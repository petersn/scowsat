// ScowSAT

#ifndef SCOWSAT_H
#define SCOWSAT_H

#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

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

struct Solver {
	std::vector<std::vector<Lit>> instance;
	std::vector<std::pair<bool, Lit>> trail;
	int committed_length = 0;
	std::vector<Assignment> assignments;
	std::vector<std::vector<int>> who_contains_this_literal;

	Solver(std::vector<std::vector<Lit>> instance);
	bool unit_propagation();
	bool solve();
	Lit decide();

	void push_assignment(bool is_decision, Lit literal);
	std::pair<bool, Lit> pop_assignment();
};

#endif

