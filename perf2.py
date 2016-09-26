#!/usr/bin/python

from subprocess import Popen, PIPE, call
from threading import Thread
from itertools import izip

import time
import os
import re
import sys

to_mega = lambda x: x / 1000000

HOST_MACHINE_IP = "146.148.5.244"#"193.136.166.130" #"194.210.223.131"


## Global string for compiling regex strings, 
## used in: __split_source_from_csv(..) and __mod_csv(..)
match_str = ("(?P<time>\d+),(?P<srcipv4>\d+\.\d+\.\d+\.\d+),(?P<srcport>\d+),"
			"(?P<dstipv4>\d+\.\d+\.\d+\.\d+),(?P<dstport>\d+),(?P<id>\d+),"
			"(?P<interval_start>\d+\.\d+)-(?P<interval_end>\d+\.\d+),"
			"(?P<size>\d+),(?P<speed>\d+)")



## iperf client command:
##  -c : ip address to connect to
##	-t : duration in seconds of client
##  -Z : congestion algorithm for TCP use
##  -y C: output to stdout in csv format
##	-i 1: interval in seconds between iperf logging (1 sec)
default_cmd = "iperf -c {} -t {} -Z {} -y C -i 1"

target = HOST_MACHINE_IP 
time_p_client = 5
num_clients = 500



def sequence_test(filename, protocol="heracles"):
	"""
		Sequence test, connections appear in sequence, ending after the next one starts
	"""
	client = lambda time, id: call(default_cmd.format(target, time, protocol).split(), stdout=f)
	f = open(filename, "w")

	threads = [Thread(target=client, args=(time_p_client, i+1)) for i in range(num_clients)]

	if time_p_client < 2: 
		raise Exception("Time per client must be higher than 2.")

	i = 0
	thr[i].start()
	while i + 1 < len(threads):
		time.sleep(time_p_client - 1)
		thr[i+1].start()
		thr[i].join()
	
	thr[-1].join()

	f.close()




def master_slave_test(filename, protocol="heracles"):
	"""
		Master slave test, a single long lived TCP connection is used,
	 	with short lived tcp connections, starting and ending in the same time.

	 	@filename - filename used to create iperf ouput csv file

	"""
	client = lambda time, id: call(default_cmd.format(target, time, protocol).split(), stdout=f)
	f = open(filename, "w")

	master = Thread(target=client, args=(num_clients * (time_p_client + 1) + 1, 0))
	master.start()

	threads = [Thread(target=client, args=(time_p_client, i+1)) for i in range(num_clients)]
	time.sleep(0.5)
	for i, thr in enumerate(threads):
		print "{}% - ({}/{})".format((i+1)*100 / num_clients,i+1,num_clients)
		thr.start()
		thr.join()
		time.sleep(1)

	master.join()
	f.close()



def __mod_csv(filename, mean_points=3):
	"""
		Function creates a new csv file from another csv, 
		calculating the moving average for each point

		WARN: it generates empty files when mean_points 
		is bigger than the number of points given

		@filename - filename of csv from which to generate a moving average
		@mean_points - number of points used to calculate the moving average

	"""	
	matcher_ordered_csv = re.compile("(?P<rel_time>\d+),{}".format(match_str))


	with open("csvs/" + filename + ".csv") as f:
		data = f.read()

	arr = list(re.finditer(matcher_ordered_csv, data))
	arr = izip(*[arr[i:] for i in range(mean_points)])

	new_csv = []
	for entry in arr:

		avg = sum(map(lambda hit: int(hit.group("speed")), entry))/float(mean_points)
		time = int(entry[0].group("rel_time"))

		new_csv.append("{},{}".format(time, to_mega(avg)))


	if "mean_speed" not in os.listdir("csvs/"):
		os.mkdir("csvs/mean_speed")

	with open("csvs/mean_speed/" + filename + "_speed.csv", "w+") as f:
		f.write("\n".join(new_csv))



## Create multiple csv files, 1 for each connection


def __split_source_port_from_csv(filename):
	"""
		Takes a csv file, created from the iperf output, 
		splitting the file into multiple csvs, based on the source port field
		(source port -> different clients)

		Also adds a relative time counter to each line of the csv (first column),
		this counter is used to time each log individually 
		and have them correspond in time when creating the graph.

		@filename - name of the joint csv file to be split

		@returns - list of names used to generate files in folder csvs/ 
				(the names in the list don't have the '.csv' extesion attached,
				before opening them attach '.csv' (i.e.: open(filename + ".csv"))) 
	"""
	with open(filename) as f:
		data = f.read()


	## rel_time_counters
	## - we need to mantain the order of connections and the relative time for each different connection 
	## (different src port)
	## - To do this, for every new connection, the counter starts after the last connection started (or 0 if no connection)
	## - For every new sample, counters are added individually, respective to the sample received

	rel_time_counters = {}
	files = []
	matcher_base_csv = re.compile(match_str)
	
	for line in data.split("\n"):
		s = re.match(matcher_base_csv, line.strip())
		if not s:
			continue

		filename = s.group("srcport")
		if filename not in files:
			if len(files) > 0:
				rel_time_counters[filename] = max(rel_time_counters.itervalues()) 
			else:
				rel_time_counters[filename] = 0 
			files.append(filename)

		with open("csvs/" + filename + ".csv", "a") as f:
			f.write("{},{}\n".format(rel_time_counters[filename], line))
			rel_time_counters[filename] += 1

	return files



def __generate_csv(filename):	

	#templates for generating gnuplot readable files
	gnuplot_base_plot = "\"csvs/{}.csv\" using 1:10 with linespoints title \"{}\""
	gnuplot_base = ("set terminal png size 1920, 1080\n"
					"set output \"{fname}.png\"\n"
					"set size 1,1\n"
					"set datafile separator \",\"\n"
					"set grid y\n"
					"plot {plots}\n"
					"exit")

	if "csvs" not in os.listdir('.'):
		os.mkdir("csvs")

	files = __split_source_port_from_csv(filename)

	for file in files:
		__mod_csv(file)

	gnuplot_speed_plot =\
	 "\"csvs/{}.csv\" using 1:2 with linespoints title \"{}\""

	#generate plot lines from each csv file for gnuplot readable file
	gnuplot_plot_files = []
	for  file in files:
		plot_name = gnuplot_speed_plot.format("mean_speed/{}_speed".format(file), "mean speed")
		gnuplot_plot_files.append(plot_name)

	mean_plots = ', '.join(gnuplot_plot_files)

	with open('/tmp/gnuplot_mean.gp', "w") as f:
		f.write(gnuplot_base.format(fname="heya_speed", plots=mean_plots))

	call("gnuplot < /tmp/gnuplot_mean.gp".split())



"""
plots = ", ".join(gnuplot_base_plot.format(file, "speed") for file in files)

with open("/tmp/gnuplot.gp", "w") as f:
	f.write(gnuplot_base.format(fname="heya",plots=plots))

call("gnuplot < /tmp/gnuplot.gp".split())
"""



if __name__ == '__main__':

	# 1 argument only (for now), argument can be a special option for a test, 
	# else it is interpreted as a congestion protocol argument, and uses master slave test

	#	./perf2.py --seq 
	# 	./perf2.py reno

	protocol = "heracles"
	test = master_slave_test

	if len(sys.argv) > 1:
		if sys.argv[1] == "--seq": # sequential mode
			print "Still not finished, come back in a month or two"
			sys.exit(0)
			#test = ...
		else:
			protocol = sys.argv[1]

	print "Starting: {}".format(protocol)

	test("/tmp/perf2file", protocol=protocol)
	__generate_csv("/tmp/perf2file")
	## a file called heya_speed.png is created with the comparison graph

