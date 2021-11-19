
BCM_H_DIR?=/opt/vc/include/interface/

CXX=g++
CXX_OPTS=-I$(BCM_H_DIR)vcos/ \
	-I$(BCM_H_DIR)vcos/pthreads/ \
	-I$(BCM_H_DIR)vmcs_host/linux \
	-I$(BCM_H_DIR)../
	

CXXFLAGS=$(CXX_OPTS)

CFLAGS=
CC_OPTS=-lopencv_core -lopencv_imgcodecs -lopencv_imgproc -lmmal -lmmal_core -lmmal_util -lbcm_host -Wl,--as-needed 
LDFLAGS=-L/opt/vc/lib $(CC_OPTS)

INSTALL=install

OBJ_RASPIMJPEG=RaspiMJPEG.o RaspiMCam.o RaspiMUtils.o RaspiMMotion.o RaspiMCmds.o

%.o: %.c
	$(CXX) -c $(CXXFLAGS) $(CXX_OPTS) $< -o $@

all: raspimjpeg

raspimjpeg: $(OBJ_RASPIMJPEG)
	$(CXX) $(CFLAGS) $(OBJ_RASPIMJPEG) -o raspimjpeg $(LDFLAGS) $(CC_OPTS)

install:
	$(INSTALL) -m 0755 -d $(DESTDIR)/usr/local/bin
	$(INSTALL) -m 755 raspimjpeg $(DESTDIR)/usr/local/bin/
	$(INSTALL) -m 664 raspimjpeg.config $(DESTDIR)/etc/

clean:
	rm -rf raspimjpeg
	rm -rf *.o *~ *.mod
