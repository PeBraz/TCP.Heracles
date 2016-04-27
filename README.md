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



## Server 
  A TCP server is provided, allowing a congestion mechanism to be chosen, without affecting normal OS networking. To compile and run use:
  
  ```
  make -C server
  ./server/server --cong heracles --port 8000 --upload heracles.img
  ```
  
  NOTE: upload flag is required, cong is OS default and port is 8000
  NOTE2: Don't try to break it, because it will break.
  


## Client

  Multiple clients can be created to receive the file from the server above (no file is created). To compile and run:
  
  ```
  make -C server
  ./server/clients <port> <number-of-clients> 
  ```
  By default there is 1 client and port is 8000.
  


## Logging 

The heracles module prints debugging information. This information can be caught by using `dmesg -t > file.log`. Make sure to clean the **dmesg** ouput before running the clients with `dmesg -C`.

After having a log file, use **log_reader.py** to create images from the logs.

  


