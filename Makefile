swingline: swingline.c
	gcc -std=gnu99 -Wall -Wextra -g -o $@ $< -lglfw -lepoxy -lGL -lm 
clean:
	rm -f swingline
