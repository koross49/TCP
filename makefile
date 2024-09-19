
try: client server

client:
	g++ -o client client.cpp -lwsock32

server:
	g++ -o server server.cpp -lwsock32

clean:
	del client.exe
	del server.exe