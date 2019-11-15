// ScowSAT

#include <algorithm>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cassert>
#include "scowsat.h"

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

Solver::Solver(std::vector<std::vector<Lit>> instance)
	: instance(instance)
{
	Var max_var = 0;
	for (auto& clause : instance)
		for (Lit lit : clause)
			max_var = std::max(max_var, lit_to_var(lit));
	int var_count = max_var + 1;
	assignments.resize(var_count, assign_shrug);

	who_contains_this_literal.resize(2 * var_count);
	int clause_index = 0;
	for (auto& clause : instance) {
		for (Lit lit : clause)
			who_contains_this_literal[lit].push_back(clause_index);
		clause_index++;
	}
}

bool Solver::unit_propagation() {
	while (committed_length < trail.size()) {
		Lit to_apply = trail[committed_length].second;
		for (int clause_index : who_contains_this_literal[flip_sign(to_apply)]) {
			int unassigned_lits = 0;
			Lit unit_literal;
			for (Lit lit : instance[clause_index]) {
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
			if (unassigned_lits == 1)
				push_assignment(false, unit_literal);
			clause_satisfied:;
		}
		committed_length++;
	}
	return false;
}

Lit Solver::decide() {
	for (Var v = 0; v < assignments.size(); v++)
		if (assignments[v] == assign_shrug)
			return make_negative(var_to_lit(v));
	assert(false); // Oh no!
}

void Solver::push_assignment(bool is_decision, Lit literal) {
	trail.emplace_back(is_decision, literal);
	assignments.at(lit_to_var(literal)) = get_sign(literal);
}

std::pair<bool, Lit> Solver::pop_assignment() {
	auto p = trail.back();
	assignments.at(lit_to_var(p.second)) = assign_shrug;
	trail.pop_back();
	return p;
}

bool Solver::solve() {
	// TODO: Add an initial pass looking for units.
	while (true) {
		bool conflict = unit_propagation();
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
			Lit decision = decide();
			push_assignment(true, decision);
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Usage: scowsat instance.dimacs" << std::endl;
		return 1;
	}
	auto instance = load_dimacs(argv[1]);
	Solver solver(instance);
	bool sat = solver.solve();
	std::cout << (sat ? "sat!" : "unsat!") << std::endl;
	for (auto p : solver.trail)
		std::cout << " " << lit_to_var(p.second) + 1 << "=" \
			<< (get_sign(p.second) ? "⊤" : "⊥");
	std::cout << std::endl;
}

