#!/usr/bin/python

import re
import os
import sys
from subprocess import call

COUNT=1
HID=2
IP=3
PKTS=4
CWND=5
SSTHRESH=6
RTT=7
SRTT=8
MDEV=9


gnuplot_base_plot = "\"{}.csv\" using 1:{} with linespoints title \"{}\""

gnuplot_base = ("set terminal jpeg size 1280,720\n"
	"set output \"{fname}.jpeg\"\n"
	"set size 1,1\n"
	"set grid y\n"
	"plot {plots}\n"
	"exit")

#
#gnuplot_cwnd = ("set terminal jpeg size 1280,720\n"
#	"set output \"{fname}_cwnd.jpeg\"\n"
#	"set size 1,1\n"
#	"set grid y\n"
#	"plot \"{fname}.csv\" using 1:5 with linespoints title \"{fname}\"\n"
#	"exit")
#
#
#create_script("string_rtt", [rtt, srtt, mdev],[string.csv],[RTT, SRTT, MDEV])
#gnuplot_rtt = ("set terminal jpeg size 1280,720\n"
#	"set output \"{fname}_rtt.jpeg\"\n"
#	"set size 1,1\n"
#	"set grid y\n"
#	"plot \"{fname}.csv\" using 1:7 with linespoints title \"rtt\","
#	" \"{fname}.csv\" using 1:8 with linespoints title \"srtt\","
#	" \"{fname}.csv\" using 1:9 with linespoints title \"mdev\"\n"
#	"exit")


def create_script(name, name_list, csv_list, attr_list):
	plots = ', '.join([gnuplot_base_plot.format(*vals) 
				for vals in zip(csv_list, attr_list, name_list)])
	return gnuplot_base.format(fname=name, plots=plots)

#
# @join boolean		if true then numbering will intercalate index values
#						between files, else numbering is independent
#
#
#	join is used when the order of events is important.
#
#	joined:                   Non-joined:
#	FILE1		FILE2				FILE1		FILE2	
#	|1	|		|2	| 				|1	|		|1	| 	
#	|3	|		|5	|		VS. 	|2	|		|2	|
#	|4	|		|6	|				|3	|		|3	|
#
def log_to_csv(log_data, intercal=False):
	d = {}
	d_id = {}
	for i, line in enumerate(log_data.split('\n')):
		if re.match( "\w+ \d+ \d+ \d+ \d+ \d+ \d+ \d+", line):
			idn = line.split()[0]
			if not d_id.has_key(idn):
				d_id[idn] = 0
			d_id[idn] += 1
			d.setdefault(idn, []).append("{} {}".format(i if intercal else d_id[idn],line))
	return d

def log_to_csv_with_time(log_data, intercal=False):
	d = {}
	time = 0
	for i, line in enumerate(log_data.split('\n')):

		patt = re.compile("\[(\d+\.\d+)\] \w+ \d+ \d+ \d+ \d+ \d+ \d+ \d+")
		match = re.match(patt, line)
		if match:
			#if not time_init: time_init = float(match.group(1))
			time = float(match.group(1)) ## remove floating time from timestamp
			idn = line.split()[1] ## remove log identifier (element 2, because 1 is time)
			line = log_line_cleanse(line)
			d.setdefault(idn, []).append("{} {}".format(time, line.split("]")[1].strip()))
	return d

#shift right rtt value 3 times
def log_line_cleanse(line):
	line_parts = line.split()
	new_srtt = str(int(line_parts[7]) >> 3)
	new_mdev = str(int(line_parts[8]) >> 2)
	return ' '.join(line_parts[:7] + [new_srtt, new_mdev])

help_str = ("usage: ./log_reader.py <log file>\n"
			"options:\n"
				"\t--help - print this information\n"
				"\t--join - concatenate information from differente connections\n")

#
#	Creates a csv file from the dmesg output
#	@logname - logging filename
#	@join - optional flag indicating if numbering should be intesected or independent
#
#	@returns  name root of the generated .csv files
#

def log_reader(logname, join=False, timestamp=False):
	fname = logname.split('.')[0]
	files = []
	
	with open(logname) as log:
		conn_log = log_to_csv_with_time(log.read(), join) if timestamp\
		 else log_to_csv(log.read(), join)
		
	for i, logs in conn_log.iteritems():
		files.append("{}_{}".format(fname, i))
		with open("{}_{}.csv".format(fname, i), 'w+') as csv:
			csv.write('\n'.join(logs))
	return files

if __name__ == '__main__':


	if len(sys.argv) < 2:
		print "No arguments given.\n" + help_str
		sys.exit(1)

	if sys.argv[1] == '--help': 
		print help_str
		sys.exit(0)

	join, time = False, False
	for arg in sys.argv[1:]:
		if arg == "--join":
			join = True 
		elif arg == "--time":
			time = True
		else:
			logname = arg


	fname = logname.split('.')[0]
	files = log_reader(logname, join, time)
	if not files:
		print "Wrong file given, no files generated."
		sys.exit(1)


	if join:

		sc2 = create_script(fname + "_rtt", ["rtt","srtt","mdev"]*len(files), files*3, [RTT,SRTT,MDEV]*len(files)) 
		sc1 = create_script(fname + "_cwnd", ["cwnd", "cwnd"], files, [CWND]*len(files));
		scripts = [sc1, sc2]	

		for script in scripts:
			with open("{}.gp".format(fname), 'w+') as f:
				f.write(script)

			call(["gnuplot" , "<", "{}.gp".format(fname)])
	else:
		for file in files:	
			sc1 = create_script(file + "_rtt", ["rtt", "srtt", "mdev"], [file]*3, [RTT,SRTT,MDEV])
			sc2 = create_script(file + "_cwnd", ["cwnd"], [file], [CWND])
			for script in [sc1, sc2]:
				with open("{}.gp".format(file), 'w+') as f:
					f.write(script.format(fname=file))

				call(["gnuplot" , "<", "{}.gp".format(file)])

