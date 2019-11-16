// ScowSAT

#ifndef SCOWSAT_H
#define SCOWSAT_H

#include <cmath>
#include <cstdint>
#include <vector>
#include <list>
#include <string>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include "thread_safe_queue.h"

typedef uint32_t Lit;
typedef uint32_t Var;
typedef uint8_t Assignment;

constexpr Assignment assign_false = 0;
constexpr Assignment assign_true  = 1;
constexpr Assignment assign_shrug = 2;

static inline Lit  make_positive(Lit lit) { return lit | 1; }
static inline Lit  make_negative(Lit lit) { return lit & -2; }
static inline bool get_sign(Lit lit)      { return lit & 1; }
static inline Lit  flip_sign(Lit lit)     { return lit ^ 1; }
static inline Lit  var_to_lit(Var var)    { return var << 1; }
static inline Var  lit_to_var(Lit lit)    { return lit >> 1; }

static inline Lit dimacs_to_lit(int x)  {
	Lit lit = make_positive(var_to_lit(std::abs(x) - 1));
	if (x < 0)
		lit = make_negative(lit);
	return lit;
}

std::vector<std::vector<Lit>> load_dimacs(std::string path);

struct Instance {
	std::vector<std::vector<Lit>> clauses;
	std::vector<std::vector<int>> literal_to_containing_clauses;
	std::vector<Lit> literals_by_importance;
	int var_count;

	Instance(std::vector<std::vector<Lit>> clauses);
};

struct ParallelSolver;

struct WorkItem;

struct Worker {
	int thread_id;
	ParallelSolver* parent;
	std::mt19937 rng;

	// This must be initialized last, to make sure the above data is available to the thread.
	std::thread t;

	Worker(int thread_id, ParallelSolver* parent);
	void join();
	static void thread_main(Worker* self);
	static bool do_work(Worker* self, WorkItem& work);
};

struct ParallelSolver {
	std::mutex solver_mutex;
	thread_safe_queue<WorkItem> work_queue;
	std::atomic<int> work_items{0};
	std::atomic<bool> found_solution{false};
	std::list<Worker> workers;
	Instance instance;
	int trail_cutoff;

	ParallelSolver(Instance&& instance, int thread_count);
	void solve();
	void join();
	void send_kill_signals();
};

struct SolverState {
	std::vector<std::pair<bool, Lit>> trail;
	int committed_length = 0;
	std::vector<Assignment> assignments;

	SolverState() {}
	SolverState(const Instance& instance);

	bool initial_processing(const Instance& instance);
	bool unit_propagate(const Instance& instance);
	bool solve(const Instance& instance);
	Lit decide(const Instance& instance, uint32_t randomness);

	void push_assignment(bool is_decision, Lit literal);
	std::pair<bool, Lit> pop_assignment();
};

struct WorkItem {
	bool do_die;
	SolverState state;
};

#endif

