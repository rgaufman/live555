#include "NetCommon.h"
#include <unistd.h>

int closeSocket(int fd){
    return close(fd);
}
