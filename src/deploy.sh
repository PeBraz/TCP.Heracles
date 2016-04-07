vm=root@188.186.12.172
heracles=~/Desktop/TESE/TCP.Heracles

scp -r $(heracles) $(vm)
ssh $(vm)


make -f $(heracles)/src/tests/Makefile
insmod tester.ko


