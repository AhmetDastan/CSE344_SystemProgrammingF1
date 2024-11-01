All: compile clean

compile: clientGenerator.c server.c
	@gcc server.c -o serverExe -lpthread -lm
	@gcc clientGenerator.c -o clientExe

runServer:
	@./serverExe 127.10.1.1 8181 4 4 5

runClient:
	@./clientExe 127.10.1.1 8181 30 15 15

clean: 
	@rm -f server.log ce se
