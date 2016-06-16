# TCP.Heracles
TCP congestion mechanism for the Linux kernel.

To compile and add module:
```
cd src
make
make install
```

To remove and clean module:

```
cd src
make uninstall
make clean
```

## Testing Locally

To run Heracles locally we use a [Netkit](http://wiki.netkit.org/index.php/Main_Page) instance, to which we connect using a tap interface. This connection can then be throttled by calling `make limit` inside the *src* folder (lantecy 100ms/throughput 5Mbps). 

The netkit instance only needs to run an [iperf2](https://iperf.fr/) server, using `iperf -s`. From the main directory, the script `perf_test.py` connects to the server and runs multiple iperf clients (the target ip and number of clients can be configured).

Png files with connection statistics are created inside the `log` folder.




### Server - deprecated

  ```
    Deprecated - the server code can't optimize tcp correctly to use the maximum avaiable window.
  ```

  A TCP server is provided, allowing a congestion mechanism to be chosen, without affecting normal OS networking. To compile and run use:
  
  ```
  make -C server
  ./server/server --cong heracles --port 8000 --upload heracles.img
  ```
  
  - NOTE: upload flag is required, cong is OS default and port is 8000
  
  - NOTE2: Don't try to break it, because it will break.
  
  


### Client - deprecated

  Multiple clients can be created to receive the file from the server above (no file is created). To compile and run:
  
  ```
  make -C server
  ./server/clients <port> <number-of-clients> 
  ```
  By default there is 1 client and port is 8000.
  


### Logging 

The heracles module prints debugging information. This information can be caught by using `dmesg -t > file.log`. Make sure to clean the **dmesg** ouput before running the clients with `dmesg -C`.

After having a log file, use **log_reader.py** to create images from the logs.

  


