#!/usr/bin/python

import subprocess, glob, json, sys, argparse

devnull = open("/dev/null")
retcode_to_string = {10: "sat", 20: "unsat"}

def populate_answers():
	devnull = open("/dev/null")

	mapping = {}
	for path in sorted(glob.glob("test_instances/*.dimacs")):
		print(path)
		retcode = subprocess.call(["minisat", path], stdout=devnull)
		mapping[path] = retcode_to_string[retcode]

	with open("test_instances/answers", "w") as f:
		json.dump(mapping, f, indent=2)
		f.write("\n")

	print("Satisfiable fraction:", sum(a == "sat" for a in mapping.values()) / len(mapping))

def test_sat_solver(solver_path):
	with open("test_instances/answers", "r") as f:
		mapping = json.load(f)

	any_failures = False
	for path in sorted(glob.glob("test_instances/*.dimacs")):
		var_count = int(path.split("vars")[1].split("_")[0])
		print(path, end=" ")
		sys.stdout.flush()
		retcode = subprocess.call([solver_path, path], stdout=devnull)	
		if retcode_to_string[retcode] == mapping[path]:
			print("pass")
		else:
			print("FAILURE!!!")
			any_failures = True
	if any_failures:
		exit(1)

parser = argparse.ArgumentParser()
parser.add_argument("--populate-answers", action="store_true", help="Run minisat on all of the test instances, and cache the answers.")
parser.add_argument("--solver", default="./scowsat", type=str, help="Path to the solver to test.")

if __name__ == "__main__":
	args = parser.parse_args()

	if args.populate_answers:
		print("Populating reference answers cache.")
		populate_answers()
		exit()

	print("Testing", args.solver)
	test_sat_solver(args.solver)

