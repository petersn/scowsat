// ScowSAT
// Known bugs:
//   1) If the input DIMACS file doesn't have at least one octet after the final 0 on the final clause then we fail to parse the final clause.
//   2) If the input DIMACS file contains any unit or empty clauses then we'll do the wrong thing.

#include <algorithm>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include <unistd.h>
#include "scowsat.h"

constexpr int CUTOFF = 128;

static std::random_device global_random_device;

#ifdef DEBUG_COUNTS
std::atomic<long long> decisions{0};
std::atomic<long long> queue_insertions{0};
std::atomic<long long> units_found{0};
#endif

std::vector<std::vector<Lit>> load_dimacs(std::string path) {
	std::vector<std::vector<Lit>> instance;

	int expected_variables = -1, expected_clauses = -1;

	std::ifstream infile(path);
	std::string line;
	while (std::getline(infile, line)) {
		std::istringstream iss(line);
		std::string token;
		iss >> token;
		if (token == "" or token == "c")
			continue;
		if (token == "p") {
			std::string kind;
			iss >> kind;
			if (kind != "cnf" and kind != "CNF")
				throw std::runtime_error("DIMACS kind must be CNF, was: " + kind);
			iss >> expected_variables >> expected_clauses;
			break;
		}
		throw std::runtime_error("Bad token in DIMACS file: " + token);
	}

	std::unordered_set<Var> all_variables;

	// Parse all the clauses.
	std::stringstream iss;
	iss << infile.rdbuf();
	while (not iss.eof()) {
		std::vector<Lit> clause;
		while (true) {
			int dimacs_var = 0;
			iss >> dimacs_var;
			if (dimacs_var == 0)
				break;
			Lit lit = dimacs_to_lit(dimacs_var);
			clause.push_back(lit);
			all_variables.insert(lit_to_var(lit));
		}
		// XXX: FIXME: This has the wrong behavior (drops the last clause) if the file
		// doesn't contain at least one character (such as a newline) after the last 0.
		// TODO: Fix this.
		if (not iss.eof())
			instance.emplace_back(std::move(clause));
	}

	if (all_variables.size() != expected_variables)
		std::cerr << "Warning: DIMACS header variable count mismatch. Expected: " \
			<< expected_variables << " got " << all_variables.size() << std::endl;
	if (instance.size() != expected_clauses)
		std::cerr << "Warning: DIMACS header clause count mismatch. Expected: " \
			<< expected_clauses << " got " << instance.size() << std::endl;

	return instance;
}

template <typename T>
std::ostream& operator << (std::ostream& o, std::vector<T> v) {
	o << "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i != 0)
			o << ", ";
		o << v[i];
	}
	o << "]";
	return o;
}

template <typename T, typename U>
std::ostream& operator << (std::ostream& o, std::pair<T, U> p) {
	o << "(" << p.first << ", " << p.second << ")";
	return o;
}

// ==========

Instance::Instance(std::vector<std::vector<Lit>> clauses)
	: clauses(clauses)
{
	Var max_var = 0;
	for (auto& clause : clauses)
		for (Lit lit : clause)
			max_var = std::max(max_var, lit_to_var(lit));
	var_count = static_cast<int>(max_var) + 1;

	std::vector<std::pair<Lit, double>> literal_occurrences;
	for (Lit lit = 0; lit < 2 * var_count; lit++)
		literal_occurrences.emplace_back(lit, 0);

	literal_to_containing_clauses.resize(2 * var_count);
	int clause_index = 0;
	for (auto& clause : clauses) {
		for (Lit lit : clause) {
			literal_to_containing_clauses[lit].push_back(clause_index);
			literal_occurrences[lit].second += 1.01;
			literal_occurrences[flip_sign(lit)].second += 1;
		}
		clause_index++;
	}

	std::sort(
		literal_occurrences.begin(),
		literal_occurrences.end(),
		[](std::pair<Lit, double>& a, std::pair<Lit, double>& b) {
			// NB: Intentionally reversed sort.
			return a.second > b.second;
		}
	);
	std::unordered_set<Var> vars_seen;
	for (auto& p : literal_occurrences) {
		Var v = lit_to_var(p.first);
		if (vars_seen.count(v) > 0)
			continue;
		literals_by_importance.push_back(p.first);
		vars_seen.insert(v);
	}
}

Worker::Worker(int thread_id, ParallelSolver* parent)
	: thread_id(thread_id),
	  parent(parent),
	  rng(global_random_device()),
	  t(Worker::thread_main, this) {}

void Worker::join() {
	t.join();
}

void Worker::thread_main(Worker* self) {
	while (true) {
		WorkItem work = self->parent->work_queue.get();
		if (work.do_die)
			break;
		bool result = do_work(self, work);
		if (result) {
			std::cerr << "Total puts: " << self->parent->work_queue.total_puts << " ";
			exit(10);
//			std::cout << "Found solution!" << std::endl;
			self->parent->found_solution = true;
			self->parent->send_kill_signals();
			break;
		} else {
//			std::cout << "Thread found no solution." << std::endl;
		}

		// TODO: Think *very* hard about this little section here.
		self->parent->work_items--;
		if (self->parent->work_items == 0) {
			self->parent->send_kill_signals();
			break;
		}
	}
}

bool Worker::do_work(Worker* self, WorkItem& work) {
	SolverState& state = work.state;
	const Instance& instance = self->parent->instance;
	while (true) {
		bool conflict = state.unit_propagate(instance);
		if (conflict) {
			while (true) {
				if (state.trail.empty())
					return false;
				auto p = state.pop_assignment();
				if (p.first) {
					state.push_assignment(false, flip_sign(p.second));
					break;
				}
			}
			state.committed_length = state.trail.size() - 1;
		} else {
#ifdef DEBUG_COUNTS
			decisions++;
#endif
			auto trail_size = state.trail.size();
			if (trail_size == state.assignments.size())
				return true;
//			Lit decision = state.decide(instance, self->rng());
			Lit decision = state.decide(instance, 0);
			// Figure out if we should branch here.
			if (
//				false
				trail_size < self->parent->trail_cutoff and
//				(self->rng() & 0xf) == 0 and
				self->parent->work_queue.queue_length <= CUTOFF
			) {
				state.push_assignment(false, flip_sign(decision));
				self->parent->work_items++;
				self->parent->work_queue.put({false, SolverState(state)});
#ifdef DEBUG_COUNTS
				queue_insertions++;
#endif
				state.pop_assignment();
				state.push_assignment(false, decision);
			} else {
//				std::cout << "Failing to branch because:" << trail_size << " " << self->parent->trail_cutoff << " " << self->parent->work_queue.queue_length << std::endl;
				state.push_assignment(true, decision);
			}
		}
	}
}

ParallelSolver::ParallelSolver(Instance&& instance, int thread_count)
	: instance(instance) // NB: Do I need std::move again here?
{
	std::cout << "Launching parallel solver." << std::endl;
	for (int i = 0; i < thread_count; i++)
		workers.emplace_back(i, this);
}

void ParallelSolver::solve() {
	SolverState initial_state(instance);
	trail_cutoff = 0.05 * instance.var_count;
	if (workers.size() == 1)
		trail_cutoff = 0;

	if (not initial_state.initial_processing(instance))
		return;
	work_items++;
	work_queue.put({false, initial_state});
}

void ParallelSolver::send_kill_signals() {
	for (auto& _ : workers)
		work_queue.put({true});
}

void ParallelSolver::join() {
	for (auto& w : workers)
		w.t.join();
}

// ==========

SolverState::SolverState(const Instance& instance) {
	assignments.resize(instance.var_count, assign_shrug);
}

bool SolverState::initial_processing(const Instance& instance) {
	// Look for initial units or empty clauses.
	for (auto& clause : instance.clauses) {
		// An empy clause is unsatisfiable!
		if (clause.size() == 0)
			return false;
		// Push all units we find onto the queue for initial processing.
		if (clause.size() == 1) {
			Lit unit_literal = clause[0];
			if (assignments.at(lit_to_var(unit_literal)) == flip_sign(get_sign(unit_literal)))
				return false;
			push_assignment(false, clause[0]);
		}
	}
	return true;
}

bool SolverState::unit_propagate(const Instance& instance) {
	while (committed_length < trail.size()) {
		Lit to_apply = trail[committed_length].second;
		for (int clause_index : instance.literal_to_containing_clauses[flip_sign(to_apply)]) {
			int unassigned_lits = 0;
			Lit unit_literal;
			for (Lit lit : instance.clauses[clause_index]) {
				Assignment a = assignments[lit_to_var(lit)];
				if (a == assign_shrug) {
					unassigned_lits++;
					unit_literal = lit;
				} else if (a == get_sign(lit)) {
					goto clause_satisfied;
				}
			}
			if (unassigned_lits == 0)
				return true;
			if (unassigned_lits == 1) {
#ifdef DEBUG_COUNTS
				units_found++;
#endif
				push_assignment(false, unit_literal);
			}
			clause_satisfied:;
		}
		committed_length++;
	}
	return false;
}

Lit SolverState::decide(const Instance& instance, uint32_t randomness) {
#if 0
	int offset = (randomness >> 1) % assignments.size();
	for (Var v = 0; v < assignments.size(); v++) {
		Var offset_var = (v + offset) % assignments.size();
		if (assignments[offset_var] == assign_shrug) {
			return var_to_lit(offset_var) | (randomness & 1);
		}
	}
#elif 1
	for (Lit lit : instance.literals_by_importance)
		if (assignments[lit_to_var(lit)] == assign_shrug)
			return lit;
#else
	for (Var v = 0; v < assignments.size(); v++)
//	for (Lit lit : instance.literals_by_importance) {
		if (assignments[v] == assign_shrug)
			return var_to_lit(v);
	}
#endif
	assert(false); // Oh no!
}

/*
bool SolverState::solve(const Instance& instance) {
	while (true) {
		bool conflict = unit_propagate(instance);
		if (conflict) {
			while (true) {
				if (trail.empty())
					return false;
				auto p = pop_assignment();
				if (p.first) {
					push_assignment(false, flip_sign(p.second));
					break;
				}
			}
			committed_length = trail.size() - 1;
		} else {
			if (trail.size() == assignments.size())
				return true;
			Lit decision = decide(instance, 0);
			push_assignment(true, decision);
		}
	}
}
*/

void SolverState::push_assignment(bool is_decision, Lit literal) {
	trail.emplace_back(is_decision, literal);
	assignments.at(lit_to_var(literal)) = get_sign(literal);
}

std::pair<bool, Lit> SolverState::pop_assignment() {
	auto p = trail.back();
	assignments.at(lit_to_var(p.second)) = assign_shrug;
	trail.pop_back();
	return p;
}

int main(int argc, char** argv) {
	Instance instance(load_dimacs(argv[1]));
	int threads = std::thread::hardware_concurrency();
	std::cout << "Using " << threads << " threads." << std::endl;
	ParallelSolver ps(std::move(instance), threads);
	ps.solve();
	ps.join();
	std::cout << (ps.found_solution ? "SAT" : "UNSAT") << std::endl;
#ifdef DEBUG_COUNTS
	std::cerr << "Decisions: " << decisions << " Units: " << units_found << " Enqueues: " << queue_insertions << std::endl;
#endif
	std::cerr << "Total puts: " << ps.work_queue.total_puts << " ";
	return ps.found_solution ? 10 : 20;
}

#if 0
int old_main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Usage: scowsat instance.dimacs" << std::endl;
		return 1;
	}
	auto instance = load_dimacs(argv[1]);
	SolverState solver(instance);
	bool sat = solver.solve();
	std::cout << (sat ? "sat!" : "unsat!") << std::endl;
	for (auto p : solver.trail)
		std::cout << " " << lit_to_var(p.second) + 1 << "=" \
			<< (get_sign(p.second) ? "⊤" : "⊥");
	std::cout << std::endl;
	return sat ? 10 : 20;
}
#endif

