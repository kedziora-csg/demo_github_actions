//------------------------------------------------------------------------------
// hello_world_mpi -- hybrid MPI + OpenMP placement diagnostic.
//
// Prints one record per (MPI rank x OpenMP thread) giving the rank, hostname,
// thread number, and the CPU / physical core / socket / NUMA node that thread is
// actually running on.  Output is gathered to rank 0 and emitted in rank order,
// so a run inside a container and a run built natively can be diff'd directly.
//
// Build (this is the exact line used everywhere in this repo -- no -std=, no
// extra libraries, no extra include paths):
//
//     mpicxx -o hello_world_mpi hello_world_mpi.cxx -fopenmp
//
// It also builds and runs correctly *without* -fopenmp (one thread per rank),
// and as a singleton with no mpiexec.
//
// This program deliberately configures nothing: it never calls
// omp_set_num_threads(), never sets a proc_bind clause, never touches the
// environment.  Its whole value is being a passive observer of what
// OMP_NUM_THREADS / OMP_PROC_BIND / OMP_PLACES, PBS ompthreads=N and the job
// launcher's --cpu-bind actually produced.
//
// It never exits non-zero: every degraded condition is reported in-band with a
// '-' field or a '#' header note.  Several callers chain it with && during image
// builds, and oversubscription on a small CI runner is normal, not an error.
//------------------------------------------------------------------------------

// MUST come before every #include: glibc latches the feature-test macros in
// <features.h>, which the first system or libstdc++ header drags in.  An include
// hoisted above this line silently removes sched_getcpu() from <sched.h>.  The
// guard avoids a redefinition warning where the compiler driver already set it.
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE 1
#endif

#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

#include "mpi.h"

// __GLIBC_PREREQ arrives with <features.h>, so this must follow the includes.
// It has to be nested: the preprocessor is not required to short-circuit macro
// expansion in #if, so `defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2,6)` can hard
// error on the unexpanded token.
#if defined(__linux__)
#  if defined(__GLIBC__)
#    if defined(__GLIBC_PREREQ)
#      if __GLIBC_PREREQ(2, 6)
#        define HW_HAVE_SCHED_GETCPU 1
#      endif
#    endif
#  else
     // musl et al: present since forever, __linux__ is the real gate.
#    define HW_HAVE_SCHED_GETCPU 1
#  endif
#endif

namespace
{

const char *const UNAVAIL = "-";

//------------------------------------------------------------------------------
// OpenMP shims.  Privately named so we never collide with a compiler that
// declares the omp_* entry points intrinsically.  Without -fopenmp the pragmas
// are ignored and these collapse to the serial answers, so the no-OpenMP build
// follows the *same* code path rather than a second branch.
//------------------------------------------------------------------------------
int hw_thread_num ()
{
#ifdef _OPENMP
  return omp_get_thread_num ();
#else
  return 0;
#endif
}

// Valid only inside a parallel region -- this is the actual team size, which is
// what we want.  omp_get_max_threads() called outside is merely the ceiling and
// disagrees under OMP_DYNAMIC, OMP_THREAD_LIMIT, or cpuset clamping: exactly the
// situations this tool exists to expose.
int hw_num_threads ()
{
#ifdef _OPENMP
  return omp_get_num_threads ();
#else
  return 1;
#endif
}

// omp_get_place_num() is OpenMP 4.5 (_OPENMP >= 201511).  Returns -1 when the
// thread is not bound to a place, which is itself the interesting answer.
int hw_place_num ()
{
#if defined(_OPENMP) && (_OPENMP >= 201511)
  return omp_get_place_num ();
#else
  return -1;
#endif
}

// omp_get_proc_bind() is OpenMP 4.0 (_OPENMP >= 201307).
const char *hw_proc_bind ()
{
#if defined(_OPENMP) && (_OPENMP >= 201307)
  switch (omp_get_proc_bind ())
    {
    case omp_proc_bind_false:  return "false";
    case omp_proc_bind_true:   return "true";
    case omp_proc_bind_master: return "master";
    case omp_proc_bind_close:  return "close";
    case omp_proc_bind_spread: return "spread";
    default:                   return "?";
    }
#else
  return UNAVAIL;
#endif
}

//------------------------------------------------------------------------------
// Which CPU am I on, right now?
//
// The raw syscall is the primary rather than the fallback: it has no glibc
// version dependency, exists on every Linux/arch this will ever see, and hands
// back the NUMA node for free -- which we would otherwise have to dig out of
// sysfs.  (Not libc getcpu(): that is glibc >= 2.29 only.)  It costs one real
// syscall, called once per thread.
//------------------------------------------------------------------------------
void hw_get_cpu (int &cpu, int &node)
{
  cpu = -1;
  node = -1;

#if defined(__linux__) && defined(SYS_getcpu)
  unsigned int c = 0u, n = 0u;
  // The third argument (tcache) has been ignored since Linux 2.6.24.
  if (0 == syscall (SYS_getcpu, &c, &n, (void *) 0))
    {
      cpu = (int) c;
      node = (int) n;
      return;
    }
#endif

#if defined(HW_HAVE_SCHED_GETCPU)
  cpu = sched_getcpu ();          // may still be -1 (ENOSYS) -> reported as '-'
#endif
}

//------------------------------------------------------------------------------
// Topology, read from sysfs (no hwloc -- we cannot add link dependencies).
//
//   topology/physical_package_id : the socket.
//   topology/core_id             : unique only *within* a package, and on Zen3
//                                  it is APIC-derived and sparse.  Never treat
//                                  it as a global core index.
//   topology/thread_siblings_list: the SMT siblings of this CPU.  Its first
//                                  element is a globally unique physical core
//                                  id, and its length is an SMT detector (1 =>
//                                  SMT off, which is what Derecho should show).
//                                  This is what we report as 'core'.
//
// sysfs is visible under both Docker (read-only real sysfs) and Apptainer
// ("mount sys = yes" is the default), and it is NOT cgroup-filtered -- it shows
// every CPU on the node even under a narrow cpuset, which is what we want, since
// the question is "where on the *node* did I land".  Everything degrades to '-'
// rather than failing under --contain / --no-mount sys / sandboxed runtimes.
//------------------------------------------------------------------------------
struct CpuInfo
{
  int core_id;       // topology/core_id -- package-local
  int package_id;    // topology/physical_package_id
  int phys_core;     // head of thread_siblings_list -- globally unique
  int nsmt;          // sibling count; > 1 means this CPU is an SMT thread

  CpuInfo () : core_id (-1), package_id (-1), phys_core (-1), nsmt (-1) {}
};

std::string topo_path (const int cpu, const char *const leaf)
{
  std::ostringstream os;
  os << "/sys/devices/system/cpu/cpu" << cpu << "/topology/" << leaf;
  return os.str ();
}

bool read_int_file (const std::string &path, int &out)
{
  std::ifstream f (path.c_str ());
  if (!f) return false;

  int v = 0;
  if (!(f >> v)) return false;

  out = v;
  return true;
}

// Parse a sysfs *_list value: "8-15", "0,64", "0-3,8-11" -> expanded cpu ids.
void parse_range_list (const std::string &s, std::vector<int> &out)
{
  out.clear ();

  std::istringstream fields (s);
  std::string field;
  while (std::getline (fields, field, ','))
    {
      if (field.empty ()) continue;

      const std::string::size_type dash = field.find ('-');
      if (std::string::npos == dash)
        {
          std::istringstream is (field);
          int v = 0;
          if (is >> v) out.push_back (v);
        }
      else
        {
          std::istringstream lo (field.substr (0, dash));
          std::istringstream hi (field.substr (dash + 1));
          int a = 0, b = 0;
          if ((lo >> a) && (hi >> b))
            for (int v = a; v <= b; ++v)
              out.push_back (v);
        }
    }
}

bool read_list_head (const std::string &path, int &first, int &count)
{
  std::ifstream f (path.c_str ());
  if (!f) return false;

  std::string s;
  if (!std::getline (f, s)) return false;

  std::vector<int> v;
  parse_range_list (s, v);
  if (v.empty ()) return false;

  first = v[0];
  count = (int) v.size ();
  return true;
}

// Resolve topology for exactly the CPUs the threads were *observed* on, on the
// main thread after the parallel region has finished.  This keeps ifstream out
// of the parallel region entirely, gives one place to handle failure, and reads
// only a handful of CPUs rather than all 128 on a node.
//
// Note it must be driven by the observed CPUs, not by the process affinity mask
// sampled up front: OpenMP binds threads with sched_setaffinity, which is capped
// by the cgroup cpuset and *not* by the mask the process inherited.  So with a
// launcher binding the rank narrowly and OMP_PLACES scattering the threads
// wider, threads legitimately run on CPUs outside the initial mask.
void load_topology (const std::vector<int> &cpus,
                    std::vector<CpuInfo> &topo, bool &sysfs_ok)
{
  long n = 0;
#if defined(__linux__)
  n = sysconf (_SC_NPROCESSORS_CONF);       // configured, not cgroup-limited
#endif
  if (n < 1) n = 1;

  for (size_t k = 0; k < cpus.size (); ++k)
    if (cpus[k] >= (int) n) n = cpus[k] + 1;

  topo.assign ((size_t) n, CpuInfo ());
  sysfs_ok = false;

  for (size_t k = 0; k < cpus.size (); ++k)
    {
      const int c = cpus[k];
      if (c < 0 || (size_t) c >= topo.size ()) continue;
      if (topo[(size_t) c].core_id >= 0) continue;    // already resolved

      CpuInfo &ci = topo[(size_t) c];
      if (read_int_file (topo_path (c, "core_id"), ci.core_id))
        sysfs_ok = true;
      read_int_file (topo_path (c, "physical_package_id"), ci.package_id);
      read_list_head (topo_path (c, "thread_siblings_list"),
                      ci.phys_core, ci.nsmt);
    }
}

//------------------------------------------------------------------------------
// Formatting.  Every record is tagged and every field is always present: an
// unavailable value prints '-' rather than vanishing, because a dropped field
// shifts every downstream awk $N and silently produces garbage.
//------------------------------------------------------------------------------
std::string num_or_dash (const int v)
{
  if (v < 0) return UNAVAIL;

  std::ostringstream os;
  os << v;
  return os.str ();
}

std::string env_or_unset (const char *const name)
{
  const char *const v = getenv (name);
  return (0 != v) ? std::string (v) : std::string ("<unset>");
}

const char *thread_level_name (const int level)
{
  switch (level)
    {
    case MPI_THREAD_SINGLE:     return "SINGLE";
    case MPI_THREAD_FUNNELED:   return "FUNNELED";
    case MPI_THREAD_SERIALIZED: return "SERIALIZED";
    case MPI_THREAD_MULTIPLE:   return "MULTIPLE";
    default:                    return "unknown";
    }
}

// What each thread samples about itself.  Deliberately syscall-only: no file
// I/O, no allocation, nothing that would perturb what we are measuring.
struct ThreadObs
{
  int cpu;
  int numa;
  int place;

  ThreadObs () : cpu (-1), numa (-1), place (-1) {}
};

std::string make_thread_record (const int rank, const int nranks,
                                const int tid, const int nthreads,
                                const std::string &host,
                                const ThreadObs &obs,
                                const std::vector<CpuInfo> &topo)
{
  int core = -1, socket = -1;
  if (obs.cpu >= 0 && (size_t) obs.cpu < topo.size ())
    {
      core = topo[(size_t) obs.cpu].phys_core;
      if (core < 0)
        core = topo[(size_t) obs.cpu].core_id;   // package-local, but better
                                                 // than nothing
      socket = topo[(size_t) obs.cpu].package_id;
    }

  std::ostringstream os;
  os << "thrd rank " << rank << '/' << nranks
     << " thread " << tid << '/' << nthreads
     << " host " << host
     << " cpu " << num_or_dash (obs.cpu)
     << " core " << num_or_dash (core)
     << " socket " << num_or_dash (socket)
     << " numa " << num_or_dash (obs.numa)
     << " place " << num_or_dash (obs.place)
     << '\n';
  return os.str ();
}

} // anonymous namespace


int main (int argc, char **argv)
{
  // MPI first, OpenMP second.  A thread pool created before MPI_Init binds
  // against the *pre*-init affinity mask, so anything the PMI bootstrap, a GTL
  // LD_PRELOAD, or GPU init does to the process would be invisible -- and the
  // state we want to report is the one the compute phase will see.
  //
  // FUNNELED is the honest contract: every MPI call here is made by the master
  // thread outside the parallel region.  Requesting MULTIPLE would be a lie and
  // would select Cray MPICH's slower thread-safe path, i.e. we would be
  // measuring a configuration nobody runs.
  int provided = MPI_THREAD_SINGLE;
  MPI_Init_thread (&argc, &argv, MPI_THREAD_FUNNELED, &provided);

  int nranks = 1, rank = 0;
  MPI_Comm_size (MPI_COMM_WORLD, &nranks);
  MPI_Comm_rank (MPI_COMM_WORLD, &rank);

  char hn[256];
  std::memset (hn, 0, sizeof (hn));
  if (0 != gethostname (hn, sizeof (hn) - 1))
    std::strcpy (hn, "unknown");
  const std::string host (hn);

  // Where the two names disagree is diagnostic: Apptainer shares the host UTS
  // namespace so gethostname() gives the real node, while Docker's private UTS
  // gives a container id.
  char mpi_name[MPI_MAX_PROCESSOR_NAME];
  std::memset (mpi_name, 0, sizeof (mpi_name));
  int mpi_name_len = 0;
  MPI_Get_processor_name (mpi_name, &mpi_name_len);

  // Warm-up: force the thread pool into existence and let binding settle, so we
  // are not sampling threads mid-creation.
#pragma omp parallel
  { }

  std::vector<ThreadObs> obs;
  int nthreads = 1;
  const char *procbind = UNAVAIL;

#pragma omp parallel
  {
#pragma omp single
    {
      nthreads = hw_num_threads ();
      procbind = hw_proc_bind ();
      obs.resize ((size_t) (nthreads > 0 ? nthreads : 1));
    }   // implicit barrier: the resize is visible to the whole team below

    const int tid = hw_thread_num ();
    if (tid >= 0 && (size_t) tid < obs.size ())
      {
        ThreadObs &o = obs[(size_t) tid];
        hw_get_cpu (o.cpu, o.numa);
        o.place = hw_place_num ();
      }
  }
  // Concurrent writes to *distinct* elements of a vector that is not being
  // resized are well defined -- no locking, no critical section.

  // Now that the team is gone, resolve the topology of the CPUs we actually
  // landed on and render the records.
  std::vector<int> seen_cpus;
  for (size_t i = 0; i < obs.size (); ++i)
    if (obs[i].cpu >= 0)
      seen_cpus.push_back (obs[i].cpu);

  std::vector<CpuInfo> topo;
  bool sysfs_ok = false;
  load_topology (seen_cpus, topo, sysfs_ok);

  std::string blob;
  for (size_t i = 0; i < obs.size (); ++i)
    blob += make_thread_record (rank, nranks, (int) i, nthreads, host,
                                obs[i], topo);

  // Gather every rank's text to rank 0 so there is exactly one writer of stdout
  // and the output is byte-identical run to run.  Printing from each thread
  // would interleave *within* a line (an operator<< chain is many separate
  // writes) and the launcher's stdout forwarding preserves no cross-rank order.
  //
  // Copy through vector<char>: std::string contiguity is only guaranteed from
  // C++11 and non-const data() only from C++17, and we compile with no -std=.
  std::vector<char> sendbuf (blob.begin (), blob.end ());
  if (sendbuf.empty ()) sendbuf.resize (1);      // never hand MPI a null buffer
  const int mylen = (int) blob.size ();

  // counts/displs are "significant only at root" per the standard, but some
  // implementations dereference them anyway -- allocate on every rank.
  std::vector<int> counts ((size_t) (nranks > 0 ? nranks : 1), 0);
  std::vector<int> displs ((size_t) (nranks > 0 ? nranks : 1), 0);

  MPI_Gather (&mylen, 1, MPI_INT, &counts[0], 1, MPI_INT, 0, MPI_COMM_WORLD);

  long long total = 0;
  if (0 == rank)
    for (int r = 0; r < nranks; ++r)
      {
        displs[(size_t) r] = (int) total;
        total += counts[(size_t) r];
      }

  std::vector<char> recv;
  recv.resize ((size_t) ((0 == rank && total > 0) ? total : 1));

  MPI_Gatherv (&sendbuf[0], mylen, MPI_CHAR,
               &recv[0], &counts[0], &displs[0], MPI_CHAR,
               0, MPI_COMM_WORLD);

  if (0 == rank)
    {
      std::cout << "# exe " << ((argc > 0 && argv[0]) ? argv[0] : "?") << '\n';

#if defined(MPI_VERSION) && (MPI_VERSION >= 3)
      {
        std::vector<char> libver ((size_t) MPI_MAX_LIBRARY_VERSION_STRING, '\0');
        int libver_len = 0;
        if (MPI_SUCCESS == MPI_Get_library_version (&libver[0], &libver_len))
          {
            std::string v (&libver[0],
                           (size_t) (libver_len > 0 ? libver_len : 0));
            // Keep the first line only, and strip trailing blanks: Open MPI's
            // version string ends in one, which would otherwise show up as
            // trailing whitespace in every log.
            const std::string::size_type nl = v.find ('\n');
            if (std::string::npos != nl) v.erase (nl);
            const std::string::size_type end = v.find_last_not_of (" \t\r");
            v = (std::string::npos == end) ? std::string () : v.substr (0, end + 1);
            std::cout << "# mpi " << v << '\n';
          }
      }
#endif

      std::cout << "# thread_level requested=FUNNELED provided="
                << thread_level_name (provided) << '\n';
#ifdef _OPENMP
      std::cout << "# openmp " << _OPENMP;
#else
      std::cout << "# openmp disabled";
#endif
      std::cout << " procbind " << procbind
                << " | ranks " << nranks
                << " | topology " << (sysfs_ok ? "sysfs" : "unavailable")
                << " | mpiname " << mpi_name << '\n';

      // Echoed as seen *inside* the container: a launcher using --cleanenv
      // strips these at the boundary, and this line is what makes that visible.
      std::cout << "# env"
                << " OMP_NUM_THREADS=" << env_or_unset ("OMP_NUM_THREADS")
                << " OMP_PROC_BIND=" << env_or_unset ("OMP_PROC_BIND")
                << " OMP_PLACES=" << env_or_unset ("OMP_PLACES")
                << " PBS_JOBID=" << env_or_unset ("PBS_JOBID") << '\n';

      std::cout << "# columns: thrd rank r/nranks thread t/nthreads host <n>"
                << " cpu <i> core <i> socket <i> numa <i> place <i>"
                << "   ('" << UNAVAIL << "' = unavailable)" << '\n';

      if (total > 0) std::cout.write (&recv[0], (std::streamsize) total);
      std::cout.flush ();
    }

  MPI_Finalize ();

  // Always 0: several callers chain this with && during an image build, and a
  // degraded environment is something to report, not to fail on.
  return 0;
}
