#!/usr/bin/python

from subprocess import call
from threading import Thread
import time
import os
import re
import sys


NUM_CLIENTS = 2

target = "11.0.0.2"
protocol = "heracles"



def init():
	if call("sudo ip tcp_metrics flush".split()):
		sys.exit(1)
	call("sudo make -C src uninstall".split())
	call("sudo make -C src install".split())
	#call("sudo dmesg -C".split())


#	Returns the number in a tryx.log file or 0, if not a try.log file
def by_try_log(entry):	
	m = re.match("try(\d+)\.log", entry)
	if m:
		return int(m.group(1))
	return 0



client = lambda t, time, proto: call("iperf -c {} -t {} -Z {}".format(t,str(time),proto).split())

init()

master = Thread(target=client, args=(target, NUM_CLIENTS * 2 + 1, protocol))
master.start()

threads = [Thread(target=client, args=(target, 1, protocol)) for i in range(NUM_CLIENTS)]
time.sleep(0.1)
for thr in threads:
	thr.start()
	time.sleep(2)
	thr.join()

master.join()


## save dmesg into log
entry = max(os.listdir("log/"), key=by_try_log)
try_index = by_try_log(entry) # not incrementing the index will overwrite the most recent file


call("dmesg > log/try{}.log".format(try_index), shell=True)
print "created log: try{}.log".format(try_index)

call("make -C log clean".split())

## generate logging pictures
call("python log/log_reader.py log/try{}.log --join".format(try_index).split())