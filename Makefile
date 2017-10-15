npsh: npsh.c run.o jobs.o builtin.o jobs.h run.h builtin.h
	cc -o npsh npsh.c run.o jobs.o builtin.o
builtin.o: builtin.h jobs.h run.h
run.o: builtin.h jobs.h run.h 
jobs.o: jobs.h
