/*
Copyright (c) 2015, Broadcom Europe Ltd
Copyright (c) 2015, Silvan Melchior
Copyright (c) 2015, Robert Tidey
Copyright (c) 2015, James Hughes
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * \file RaspiMMotions.c
 * Motion detect processing for RaspiMJPEG.c
 * Relies on a feed of motion vector data from RaspiMCam.
 *
 * \date 18th April 2015
 * \Author: Silvan Melchior / Robert Tidey
 *
 * Description
 *
 * Usage information in README_RaspiMJPEG.md
 */
#include "RaspiMJPEG.h"

int motion_width, motion_height, motion_img_width, motion_img_height;
int motion_init_count;
int motion_frame_count;
int motion_changes;
int motion_state; // 0 search for start, 1 search for stop
int vector_buffer_index;
int mask_valid = 0;
int mask_disabled = 0;
unsigned char *vector_buffer;
unsigned char *mask_buffer_mem, *mask_buffer=0;

// initialise variables, set up mask buffer from a pgm file if present
void setup_motiondetect() {
   FILE *mask_file;
   int mask_size, mask_len;
   
   if (mask_buffer != 0 && mask_buffer_mem != 0)
      free(mask_buffer_mem);
 
   if (vector_buffer != 0) {
      free(vector_buffer);
      vector_buffer = 0;
   }
   mask_valid = 0;
   mask_disabled = 0;
   
   if (cfg_val[c_motion_external] != 1) {
      mask_size = motion_width * motion_height;
      printLog("Set up internal detect width=%d height=%d\n", motion_width, motion_height);
      if (cfg_val[c_motion_file])
         vector_buffer = (unsigned char *)malloc(mask_size * 4 * VECTOR_BUFFER_FRAMES);
      
	  motion_init_count = cfg_val[c_motion_initframes];
      motion_frame_count = 0;
      motion_state = 0;
      vector_buffer_index = 0;
      
      if (cfg_stru[c_motion_image] != 0) {
         mask_file = fopen(cfg_stru[c_motion_image], "r");
		 if (mask_file != NULL) {
			 mask_buffer_mem = (unsigned char *)malloc(mask_size + 256);
			 mask_len = fread(mask_buffer_mem, sizeof *mask_buffer_mem, mask_size + 256, mask_file);
			 fclose(mask_file);
			 //Check for size and header
			 if ((mask_len > mask_size + 10) && (*mask_buffer_mem == 'P') && (*(mask_buffer_mem + 1) == '5')) {
				//search for mask size string, data should be 1 byte after this
				mask_buffer = strstr(mask_buffer_mem, "255");
				if (mask_buffer != NULL) {
				   mask_buffer +=3;
				   //check size from this point
				   if ((mask_buffer_mem + mask_len - mask_buffer) >= mask_size) {
					  mask_valid = 1;
				   }
				}
			 }
			 if (!mask_valid) {
				free(mask_buffer_mem);
				mask_buffer = 0;
				error("invalid motion mask", 0);
			 } else {
				printLog("Motion mask %s loaded\n", cfg_stru[c_motion_image]);
			 }
		 } else {
			printLog("Can't open mask_image %s. Full path needed.\n", cfg_stru[c_motion_image]); 
		 }
      }
   }
}

void send_motion_start() {
   exec_macro(cfg_stru[c_motion_event], "1");
   if(cfg_val[c_motion_external] == 0) {
	   send_schedulecmd("1");
   } else if(cfg_val[c_motion_external] == 2){
	   printLogEx(c_motion_logfile, "Motion start detected\n");
   }
}

void send_motion_stop() {
   exec_macro(cfg_stru[c_motion_event], "0");
   if(cfg_val[c_motion_external] == 0) {
	   send_schedulecmd("0");
   } else if(cfg_val[c_motion_external] == 2){
	   printLogEx(c_motion_logfile, "Motion stop detected\n");
   }
}

void analyse_vectors(MMAL_BUFFER_HEADER_T *buffer) {
   if(cfg_val[c_motion_external] != 1) {
	  if(motion_init_count < 1) {
		  if(buffer->length >= (4 * motion_width * motion_height)) {
			 if (cfg_val[c_motion_detection] || cfg_val[c_motion_external] == 2 ) {
				if (cfg_val[c_motion_noise] < 1000) {
				   analyse_vectors1(buffer);
				} else {
				   analyse_vectors2(buffer);
				}
			 }
			 if (cfg_val[c_motion_file])
				save_vectors(buffer);
		  } else {
			  printLog("Unexpected vector buffer size %d\n", buffer->length);
		  }
	  } else {
		  motion_init_count--;
	  }
   }
}

void analyse_vectors1(MMAL_BUFFER_HEADER_T *buffer) {
   unsigned char *data = buffer->data;
   unsigned char high_noise = 255 - cfg_val[c_motion_noise], low_noise = cfg_val[c_motion_noise];
   int i, m, row, col;
   i = 0;
   m = 0;
   motion_changes = 0;
   for(row=0; row<motion_height; row++) {
      for(col=0; col<motion_width; col++) {
         if (mask_disabled == 1 || mask_valid == 0 || mask_buffer[m]) {
            if(data[i] > low_noise && data[i] < high_noise) motion_changes++;
            if(data[i+1] > low_noise && data[i+1] < high_noise) motion_changes++;
         }
		 m++;
         i+=4;
      }
   }
   switch (motion_state) {
      case 0:
         if (motion_changes >= cfg_val[c_motion_threshold]) {
            motion_frame_count++;
            if (motion_frame_count >= cfg_val[c_motion_startframes]) {
               send_motion_start();
               motion_frame_count = 0;
               motion_state = 1;
            }
         } else {
            motion_frame_count = -2;
         }
         break;
      case 1:
         if (motion_changes < cfg_val[c_motion_threshold]) {
            motion_frame_count++;
            if (motion_frame_count >= cfg_val[c_motion_stopframes]) {
               send_motion_stop();
               motion_frame_count = 0;
               motion_state = 0;
            }
         } else {
            motion_frame_count = -2;
         }
         break;
   }
   if (motion_frame_count < 0) motion_frame_count = 0;
}

void analyse_vectors2(MMAL_BUFFER_HEADER_T *buffer) {
   unsigned char *data = buffer->data;
   float filter = cfg_val[c_motion_noise] - 999;
   int i, m, row, col, vectorsum, clip;
   int buffer_width = 4 * motion_width;
   i = buffer_width+4;
   m = 0;
   vectorsum = 0;
   for(row=1; row<(motion_height-1); row++) {
      for(col=1; col<(motion_width-1); col++) {
         if (mask_disabled == 1 || mask_valid == 0 || mask_buffer[m]) {
            if( data[i-4] && data[i+4] && data[i-buffer_width] && data[i+buffer_width] ) {
               if(data[i] < 128) vectorsum += data[i]; else vectorsum += (256-data[i]);
               if(data[i+1] < 128) vectorsum += data[i+1]; else vectorsum += (256-data[i+1]);
            }
         }
		 m++;
         i+=4;
      }
   }
   // clip vectorsum at threee threshold to stop large bursts triggering limited to 200 ->5000%
   clip = cfg_val[c_motion_clip];
   if (clip < 2) clip = 2;
   if (clip > 50) clip = 50;
   if (vectorsum > (clip * cfg_val[c_motion_threshold])) vectorsum = clip * cfg_val[c_motion_threshold];
   motion_changes = (int)(motion_changes * (filter - 1) / filter + vectorsum / filter + 0.5);
   switch (motion_state) {
      case 0:
         if (motion_changes >= cfg_val[c_motion_threshold]) {
            motion_frame_count++;
            if (motion_frame_count >= cfg_val[c_motion_startframes]) {
               send_motion_start();
               motion_frame_count = 0;
               motion_state = 1;
            }
         } else {
            motion_frame_count = -2;
         }
         break;
      case 1:
         if (motion_changes < cfg_val[c_motion_threshold]) {
            motion_frame_count++;
            if (motion_frame_count >= cfg_val[c_motion_stopframes]) {
               send_motion_stop();
               motion_frame_count = 0;
               motion_state = 0;
            }
         } else {
            motion_frame_count = -2;
         }
         break;
   }
   if (motion_frame_count < 0) motion_frame_count = 0;
}

void reset_motion_state() {
	if(motion_state > 0) {
	  motion_state = 0;
	  motion_frame_count = 0;
      printLog("Reset motion state\n");
	}
}

void start_vectors(char *vectorname) {
   char *vector_temp;
   if(cfg_val[c_motion_file] && vector_buffer != 0) {
      asprintf(&vector_temp, "%s.dat", vectorname);
      printLog("Copying vector data to %s\n", vector_temp);
      vector_file = fopen(vector_temp, "a");
      free(vector_temp);
      if(vector_file == NULL) error("Could not open vector destination", 1);
      fseek(vector_file, motion_width * motion_height * 4 * VECTOR_BUFFER_FRAMES, SEEK_SET);
   }
}

void stop_vectors() {
   int index = 0;
   int buf_size = motion_width * motion_height * 4;
   if(vector_file != NULL) {
      fseek(vector_file, 0, SEEK_SET);
      for( index = 1; index <= VECTOR_BUFFER_FRAMES; index++) {
         fwrite(vector_buffer + ((vector_buffer_index + index) % VECTOR_BUFFER_FRAMES) * buf_size , 1, buf_size, vector_file);
      }
      fclose(vector_file);
      vector_file = NULL;
      memset(vector_buffer, 0, motion_width * motion_height * 4 * VECTOR_BUFFER_FRAMES);
      vector_buffer_index = 0;
   }
}

void save_vectors(MMAL_BUFFER_HEADER_T *buffer) {
  int bytes_written = buffer->length;
  
   if(cfg_val[c_motion_file]) {
      if (vector_file != NULL) {
         //Append data to vector file
         bytes_written = fwrite(buffer->data, 1, buffer->length, vector_file);
         if(bytes_written != buffer->length) error("Could not write all motion vector data", 0);
      } else {
         // write to circular buffer of vector data
         int buf_size = motion_width * motion_height * 4;
         memcpy(vector_buffer + buf_size * vector_buffer_index, buffer->data, buf_size);
         vector_buffer_index++;
         if (vector_buffer_index >= VECTOR_BUFFER_FRAMES) {
            vector_buffer_index = 0;
         }
      }
   }
}

void mask_disable(int disable) {
	mask_disabled = disable;
	printLog("mask disable %d\n", disable);
}