#!/usr/bin/python
from __future__ import division


import re
import os
import sys
from subprocess import call
from collections import OrderedDict

COUNT=1
HID=2
IP=3
PKTS=4
CWND=5
SSTHRESH=6
MSS=7
RTT=8
SRTT=9
MDEV=10
THROUGHPUT=11

gnuplot_base_plot = "\"{}.csv\" using 1:{} with linespoints title \"{}\""

gnuplot_base = ("set terminal png\n"
	"set output \"{fname}.png\"\n"
	"set size 1,1\n"
	"set grid y\n"
	"plot {plots}\n"
	"exit")


def create_script(name, name_list, csv_list, attr_list):
	plots = ', '.join([gnuplot_base_plot.format(*vals) 
				for vals in zip(csv_list, attr_list, name_list)])
	return gnuplot_base.format(fname=name, plots=plots)

# plot_throughput = lambda name: 

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

log_keys=["ts", "id", "ip", "out", "cwnd", "ss", "mss", "rtt", "srtt", "mdev"]

def log_to_csv_with_time(log_data, intercal=False):
	d = {}
	time = 0
	for i, line in enumerate(log_data.split('\n')):

		patt = re.compile("\[ *(?P<{}>\d+\.\d+)\] (?P<{}>\w+) (?P<{}>\d+) (?P<{}>\d+) "
			"(?P<{}>\d+) (?P<{}>\d+) (?P<{}>\d+) (?P<{}>\d+) (?P<{}>\d+) (?P<{}>\d+)".format(*log_keys))
		match = re.match(patt, line)
		if match:
			#if not time_init: time_init = float(match.group(1))
			time = float(match.group("ts")) ## remove floating time from timestamp
			idn = match.group("id") ## remove log identifier ( [time] id ... )
			line = log_line_cleanse(match)
			d.setdefault(idn, []).append(line)
	return d


# shift right rtt value 3 times
# shift mdev twice
# adds instant throughput /the throughput has a error, congestion window value may include losses
# the throughput calculation is done as bits per second
def log_line_cleanse(line_match):
	d = OrderedDict(zip(log_keys, line_match.groups()))
	d["srtt"] = str(int(d["srtt"]) >> 3)
	d["mdev"] = str(int(d["mdev"]) >> 2)
	d["throughput"] = str((int(d["cwnd"]) * int(d["mss"]) * 8 )/ (int(d["rtt"]) / 10**6))
	if float(d["throughput"]) > 10000000:
		d["throughput"] = "0"
	##should assert order is correct
	return ' '.join(d.values())



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

	join, time = False, True ## oops
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
		sc1 = create_script(fname + "_cwnd", ["cwnd"]*len(files), files, [CWND]*len(files))
		sc3 = create_script(fname + "_ss", ["ss"]*len(files), files, [SSTHRESH]*len(files))
		#sc3 = create_script(fname + "_throughput", ["tp"]*len(files), files, [THROUGHPUT]*len(files))
		scripts = [sc1, sc2, sc3]	

		for script in scripts:
			with open("{}.gp".format(fname), 'w+') as f:
				f.write(script)

			call(["gnuplot" , "<", "{}.gp".format(fname)])
	else:
		for file in files:	
			sc1 = create_script(file + "_rtt", ["rtt", "srtt", "mdev"], [file]*3, [RTT,SRTT,MDEV])
			sc2 = create_script(file + "_cwnd", ["cwnd"], [file], [CWND])
			sc3 = create_script(file + "_throughput", ["tp"], file, [THROUGHPUT])
			scripts = [sc1, sc2, sc3]

			for script in scripts:
				with open("{}.gp".format(file), 'w+') as f:
					f.write(script.format(fname=file))

				call(["gnuplot" , "<", "{}.gp".format(file)])

