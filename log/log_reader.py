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


gnuplot_cwnd = ("set terminal jpeg size 1280,720\n"
	"set output \"{fname}_cwnd.jpeg\"\n"
	"set size 1,1\n"
	"set grid y\n"
	"plot \"{fname}.csv\" using 1:5 with linespoints title \"{fname}\"\n"
	"exit")


#create_script("string_rtt", [rtt, srtt, mdev],[string.csv],[RTT, SRTT, MDEV])
gnuplot_rtt = ("set terminal jpeg size 1280,720\n"
	"set output \"{fname}_rtt.jpeg\"\n"
	"set size 1,1\n"
	"set grid y\n"
	"plot \"{fname}.csv\" using 1:7 with linespoints title \"rtt\","
	" \"{fname}.csv\" using 1:8 with linespoints title \"srtt\","
	" \"{fname}.csv\" using 1:9 with linespoints title \"mdev\"\n"
	"exit")


def create_script(name, name_list, csv_list, attr_list):
	plots = ', '.join([gnuplot_base_plot.format(*vals) 
				for vals in zip(csv_list, attr_list, name_list)])
	return gnuplot_base.format(fname=name, plots=plots)

#
# @intercal boolean		if true then numbering will intercalate index values
#						between files, else numbering is independent
#
#	Intercalated:                   Non-Intercalated:
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


if __name__ == '__main__':

	INTERCAL = True

	logname = sys.argv[1]
	fname = logname.split('.')[0]
	files = []
	with open(logname) as log:
		conn_log = log_to_csv(log.read(), INTERCAL)
		for i, logs in conn_log.iteritems():
			files.append("{}_{}".format(fname, i))
			with open("{}_{}.csv".format(fname, i), 'w+') as csv:
				csv.write('\n'.join(logs))

	if INTERCAL:
		sc2 = create_script(fname + "_rtt", ["rtt","srtt","mdev"]*len(files), files*3, [RTT,SRTT,MDEV]*len(files)) 
		sc1 = create_script(fname + "_cwnd", ["cwnd", "cwnd"], files, [CWND]*len(files));

		scripts = [sc1, sc2]
		for script in scripts:
			with open("{}.gp".format(fname), 'w+') as f:
				f.write(script)

			call(["gnuplot" , "<", "{}.gp".format(fname)])
	else:
		for file in files:	
			for script in scripts:
				with open("{}.gp".format(file), 'w+') as f:
					f.write(script.format(fname=file))

				call(["gnuplot" , "<", "{}.gp".format(file)])

