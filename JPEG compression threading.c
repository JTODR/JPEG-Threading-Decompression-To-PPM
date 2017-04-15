//Joseph O'Donovan
//JPEG multithread image processing 

//argv[0] is name of this program 
//argv[1] is input .jpg image
//argv[2] is output .pmm image
//argv[3] is number of threads to use
//eg:  ./run smallimage.jpg output.ppm 6


#include <pthread.h>    //Needed to use pthreads
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>	//needed for read/ write functions
#include <sys/stat.h>	//used to determine details of a file to be read in 
#include <sys/time.h>

#include <jpeglib.h>


struct bundle{
	unsigned char * image_data;
	int lowerLimit;
	int upperLimit;
	int width;
	int initial_pixel;
	unsigned int totalIntervals;
	int num_channels;
};
typedef struct bundle bundle;

void * process_image(void *t);

int main (int argc, char *argv[]) {
	//struct timeval t0, t1;
    	//gettimeofday(&t0, NULL);

	int rc, j;
	const char * NUM_THREADS_ARG = argv[3];
	int NUM_THREADS = atoi(NUM_THREADS_ARG);
	
	
	// Variables for the source jpg
	struct stat file_info;		
	
	unsigned long jpg_size;		//size of image
	unsigned char *jpg_buffer;	//input buffer

	// Variables for the decompressor - from jpeglib.h
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;

	// Variables for output buffer
	unsigned long bmp_size;		//size of buffer
	unsigned char *image_data;	//output buffer - will be used to write to the output file
	int row_stride, width, height, pixel_size;

	// Load the jpeg data from the .jpg file
	const char * filename = argv[1];	
	rc = stat(filename, &file_info);
	
	jpg_size = file_info.st_size;	//st_size is part of struct stat that returns the filesize
	jpg_buffer = (unsigned char*) malloc(jpg_size);	


	int fd = open(filename, O_RDONLY);	//O_RDONLY is makes the file read only
	
	int i = 0;
	while (i < jpg_size) {
	
		//read is a C system call to read a file into a buffer
		//fd is file descriptor
		//input jpeg is read into jpg_buffer
		//jpg_size is length of buffer
		rc = read(fd, jpg_buffer + i, jpg_size - i);	
		i += rc;
	}
	close(fd);


	cinfo.err = jpeg_std_error(&jerr);	
	jpeg_create_decompress(&cinfo);

	jpeg_mem_src(&cinfo, jpg_buffer, jpg_size);

	//Read the jpeg header, it will always be true
	rc = jpeg_read_header(&cinfo, TRUE);
	
	//Compress the image into cinfo
	jpeg_start_decompress(&cinfo);
	
	width = cinfo.output_width;
	height = cinfo.output_height;
	pixel_size = cinfo.output_components;

	printf("Image width: %d pixels\n", width);
	printf("Image height: %d pixels\n", height);
	printf("Image channels per pixel: %d \n", pixel_size);
	printf("Image will be greyscaled\n");
	printf("%d threads in use\n", NUM_THREADS);
	

	// Set up the output buffer
	bmp_size = width * height * pixel_size;
	image_data = (unsigned char*) malloc(bmp_size);

	//physical row width of array buffer - 3 channels * width of image, bytes per pixel
	row_stride = width * pixel_size;	
	
	// less than height as it takes a full row at a time
	while (cinfo.output_scanline < cinfo.output_height) {	
		unsigned char *buffer_array[1];		//jpeg_read_scanlines takes an array of buffers
		buffer_array[0] = image_data + (cinfo.output_scanline) * row_stride;

		// this will take 1 line at a time, the 1 ensures high quality
		// not having multiple buffers going at the same time
		jpeg_read_scanlines(&cinfo, buffer_array, 1);

	}
	
	jpeg_finish_decompress(&cinfo);

	jpeg_destroy_decompress(&cinfo);
	free(jpg_buffer);

	// ----------------------- Writing file out as .ppm -------------------------------
	
	// O_CREAT creates the file if it doesn't already exist
	// O_WRONLY makes the file write only 
	// 0666 defines the file as read and write by others
	const char * output_file = argv[2];   //arguement 2 in the command line will be output.ppm
	fd = open(output_file, O_CREAT | O_WRONLY, 0666);	
	char buf[1024];

	
	//writes the header infor to the header array buffer
	rc = sprintf(buf, "P6 %d %d 255\n", width, height);  	//P6 defines output as bytes into .ppm image
	write(fd, buf, rc);	//Header needs to be written first to set up image
	
	bundle bundle_array[NUM_THREADS];
	pthread_t threads[NUM_THREADS]; 
	
	double lowerLimit = 0;
	double upperLimit = height;
	int num_pixels = width * height;
	
	struct timeval t0, t1;
    	gettimeofday(&t0, NULL);
    	
	int cr, t; 
	for (t=0;t<NUM_THREADS;t++) { 
		
		//set parameters for current thread
		bundle_array[t].lowerLimit = lowerLimit + ( (upperLimit-lowerLimit) / NUM_THREADS)*t;	//starting point of each segment
		bundle_array[t].upperLimit = bundle_array[t].lowerLimit +((upperLimit-lowerLimit)/ NUM_THREADS);	//ending point of each segment
		bundle_array[t].width = width;
		bundle_array[t].initial_pixel = (num_pixels / NUM_THREADS)*t;	//initial pixel of each segment
		bundle_array[t].num_channels = pixel_size;
		bundle_array[t].image_data = image_data;	//buffer for each thread
		
		//call computation function
		rc = pthread_create(&threads[t],NULL,process_image,(void *)&bundle_array[t]); 
		
		if (rc) { 
			printf("ERROR return code from pthread_create(): %d\n",rc); 
			exit(-1); 
		} 
	} 
	
	
	for(t=0;t<NUM_THREADS;t++) { 
		pthread_join( threads[t], NULL); 
	} 
	
	//Timing done here
	gettimeofday(&t1, NULL);
	signed long totalTime = ((t1.tv_sec - t0.tv_sec)*1000000L +t1.tv_usec) - t0.tv_usec;
	//printf("Total time without threads in microseconds: %ld \n", totalTime );
	double time1 = (int)totalTime % 1000000;
	double time_seconds = time1 / 1000000;
	printf("Total time without threads: %f seconds \n", time_seconds );
	
	// Write out all pixel data from buffer to output file
	write(fd, image_data, bmp_size); 

	close(fd);
	free(image_data);
	

	return 0;
}



void * process_image(void *i){

	bundle *my_thread = (bundle*)i;
	
	int pixel = my_thread->initial_pixel;
	int pixel_size = my_thread->num_channels;
	
	int red, green, blue; 
	red = green = blue = 0;
	int sum = 0;
	int average = 0;

	for (int col = my_thread-> lowerLimit; col < my_thread->upperLimit; col++) {
		for (int row = 0; row < my_thread->width; row++) {
			
			red = my_thread->image_data[pixel*pixel_size];
			blue = my_thread->image_data[pixel*pixel_size +1];
			green = my_thread->image_data[pixel*pixel_size +2];
			
			sum = red + blue + green;
			average = sum / 3;
			
			//Set greyscale for current pixel
			//Average values of RGB for pixel is grey scale for that pixel
			my_thread->image_data[pixel*pixel_size] = average;
			my_thread->image_data[pixel*pixel_size +1] = average;
			my_thread->image_data[pixel*pixel_size +2] = average;
			
			//uncomment these next 3 lines to print all pixels
			/*printf("%u", image_data[pixel*pixel_size]);
			printf("%u", image_data[pixel*pixel_size+1]);
			printf("%u", image_data[pixel*pixel_size+2]);*/
		
			sum = 0;
			average = 0;
			
			pixel++;
		}
		//printf("\n");
	}

	pthread_exit(NULL); 
}











