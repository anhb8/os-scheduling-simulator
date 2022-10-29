CC = gcc
CFLAGS = -I -g

OSS=oss
USER=user

OBJ1= oss.o 
OBJ2=user.o

SRC= config.h
OUTPUT=$(OSS) $(USER)

all: $(OUTPUT) 

%.o: %.c $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(OSS)): $(OBJ1) 
	$(CC) $(CFLAGS) $(OBJ1) -o $(OSS)
	
$(USER): $(OBJ2)
	$(CC) $(CFLAGS) $(OBJ2) -o $(USER)

clean:  
	rm -f $(OUTPUT) *.o *.mainlog
