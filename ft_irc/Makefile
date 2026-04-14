NAME    = ircserv

CXX     = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

SRCS    = src/main.cpp \
          src/Server.cpp \
          src/Client.cpp \
          src/Channel.cpp \
          src/CommandHandler.cpp

OBJS    = $(SRCS:.cpp=.o)

INCLUDES = -I include

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
