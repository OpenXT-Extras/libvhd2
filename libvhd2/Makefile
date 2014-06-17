#
# Copyright (c) 2012 Citrix Systems, Inc.
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

#-------- libvhd2 makefile -------------

LIBVHD2-MAJOR 	= 1.0
LIBVHD2-MINOR 	= 0

LIBVHD2_NAME	= libvhd2.so
LIBVHD2_SO_NAME	= $(LIBVHD2_NAME).$(LIBVHD2-MAJOR)
LIBVHD2_TARGET 	= $(LIBVHD2_SO_NAME).$(LIBVHD2-MINOR) 

#--- installation directory
INSTALL_DIR		= /usr/lib

#-- generic compiler flags
CC 		= g++
CFLAGS  = -Wall

#-- generate position-independent code for the shared library
CFLAGS += -fPIC

#-- compiler flags for RELEASE mode
CFLAGS_RELEASE = -D NDEBUG -O2

#-- compiler flags for DEBUG mode
CFLAGS_DEBUG = -g

#-- external libraries
EXT_LIBS := -luuid

#-- path to source files
vpath %.cpp src
vpath %.c 	src
vpath %.h 	src
vpath %.h 	include

#-- source files list
LIB-SRCS := block_mng.cpp
LIB-SRCS += data_structures.cpp
LIB-SRCS += libvhd2.cpp
LIB-SRCS += utils.cpp
LIB-SRCS += vhd_create.cpp
LIB-SRCS += vhd_file.cpp
LIB-SRCS += vhd_file_coalesce.cpp
LIB-SRCS += vhd_file_diff.cpp
LIB-SRCS += vhd_file_dynamic.cpp
LIB-SRCS += vhd_file_fixed.cpp

#-- object files list
LIB-OBJS = $(patsubst %.cpp,%.o,$(LIB-SRCS))



#------ make release build by default
all: release

#------ explicit release build
release: CFLAGS += $(CFLAGS_RELEASE)
release: $(LIBVHD2_TARGET)

#------ explicit DEBUG build
debug:	CFLAGS += $(CFLAGS_DEBUG)
debug: 	$(LIBVHD2_TARGET)


#------ clean binary files, don't remove installed library files
clean:
	@rm -f *.o *.so.*

#------ clean binary files, remove installed library files
reallyclean: clean
	@rm -f $(DESTDIR)$(INSTALL_DIR)/$(LIBVHD2_NAME)* 

#------ install library files, $(LIBVHD2_TARGET) should be built already
install:
	install -m0644 -p $(LIBVHD2_TARGET) $(DESTDIR)$(INSTALL_DIR)
	ln -sf $(LIBVHD2_TARGET) $(DESTDIR)$(INSTALL_DIR)/$(LIBVHD2_SO_NAME)
	ln -sf $(LIBVHD2_SO_NAME) $(DESTDIR)$(INSTALL_DIR)/$(LIBVHD2_NAME)
#?? run ldconfig after ??	

#------ build shared object binary
$(LIBVHD2_TARGET): $(LIB-OBJS)
	$(CC) -shared -Wl,-soname,$(LIBVHD2_SO_NAME) -o $@ $(EXT_LIBS) $^


#-- *.cpp -> *.o pattern rule
%.o: %.cpp
	$(CC) $(CFLAGS) -c $+


.PHONY: all build release debug clean install reallyclean

