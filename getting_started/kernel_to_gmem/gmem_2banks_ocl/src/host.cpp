/**********
Copyright (c) 2018, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include "xcl2.hpp"
#include <vector>
#include "bitmap.h"

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3)
    {
        std::cout << "Usage: " << argv[0] << " <input bitmap> <golden bitmap(optional)>" << std::endl;
        return EXIT_FAILURE ;
    }
    std::string bitmapFilename = argv[1];
 
    //Read the input bit map file into memory
    BitmapInterface image(bitmapFilename.data());
    bool result = image.readBitmapFile() ;
    if (!result)
    {
        std::cout << "ERROR:Unable to Read Input Bitmap File "
            << bitmapFilename.data() << std::endl;
        return EXIT_FAILURE ;
    }
    int width = image.getWidth() ;
    int height = image.getHeight() ;
   
    //Allocate Memory in Host Memory 
    size_t image_size = image.numPixels();
    size_t image_size_bytes = image_size * sizeof(int); 
    std::vector<int,aligned_allocator<int>> inputImage(image_size);
    std::vector<int,aligned_allocator<int>> outImage(image_size);
    
    // Copy image host buffer
    memcpy(inputImage.data(), image.bitmap(), image_size_bytes);

//OPENCL HOST CODE AREA START

    std::vector<cl::Device> devices = xcl::get_xil_devices();
    cl::Device device = devices[0];

    cl::Context context(device);
    cl::CommandQueue q(context, device, CL_QUEUE_PROFILING_ENABLE);
    std::string device_name = device.getInfo<CL_DEVICE_NAME>(); 

    std::string binaryFile = xcl::find_binary_file(device_name,"apply_watermark");
    cl::Program::Binaries bins = xcl::import_binary_file(binaryFile);
    devices.resize(1);
    cl::Program program(context, devices, bins);
    cl::Kernel krnl_applyWatermark(program,"apply_watermark");

    // For Allocating Buffer to specific Global Memory Bank, user has to use cl_mem_ext_ptr_t
    // and provide the Banks 
    cl_mem_ext_ptr_t inExt, outExt;  // Declaring two extensions for both buffers
    inExt.flags  = XCL_MEM_DDR_BANK0; // Specify Bank0 Memory for input memory
    outExt.flags = XCL_MEM_DDR_BANK1; // Specify Bank1 Memory for output Memory
    inExt.obj   = inputImage.data(); 
    outExt.obj  = outImage.data(); // Setting Obj and Param to Zero
    inExt.param = 0 ; outExt.param = 0; 

    //Allocate Buffer in Bank0 of Global Memory for Input Image using Xilinx Extension
    cl::Buffer buffer_inImage(context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR | CL_MEM_EXT_PTR_XILINX,
            image_size_bytes, &inExt);
    //Allocate Buffer in Bank1 of Global Memory for Input Image using Xilinx Extension
    cl::Buffer buffer_outImage(context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR | CL_MEM_EXT_PTR_XILINX,
            image_size_bytes, &outExt);

    //Copy input Image to device global memory
    q.enqueueMigrateMemObjects({buffer_inImage}, 0 /* 0 means from host*/); 
 
    krnl_applyWatermark.setArg(0, buffer_inImage);
    krnl_applyWatermark.setArg(1, buffer_outImage);
    krnl_applyWatermark.setArg(2, width);
    krnl_applyWatermark.setArg(3, height);

    //Launch the Kernel
    q.enqueueTask(krnl_applyWatermark);

    //Copy Result from Device Global Memory to Host Local Memory
    q.enqueueMigrateMemObjects({buffer_outImage}, CL_MIGRATE_MEM_OBJECT_HOST);
    q.finish();
//OPENCL HOST CODE AREA END

    //Compare Golden Image with Output image
    bool match = 1;
    if(argc == 3)
    { 
        std::string goldenFilename = argv[2];
    	//Read the golden bit map file into memory
	BitmapInterface goldenImage(goldenFilename.data());
    	result = goldenImage.readBitmapFile() ;
    	if (!result)
	 {
           std::cout << "ERROR:Unable to Read Golden Bitmap File " << goldenFilename.data() << std::endl;
           return EXIT_FAILURE ;
    	 }
    	if ( image.getHeight() != goldenImage.getHeight() || image.getWidth() != goldenImage.getWidth()){
        	match = 0;
    	}else{
        	int* goldImgPtr = goldenImage.bitmap();
        	for (unsigned int i = 0 ; i < image.numPixels(); i++){
            	if (outImage[i] != goldImgPtr[i]){
               		match = 0;
	                printf ("Pixel %d Mismatch Output %x and Expected %x \n", i, outImage[i], goldImgPtr[i]);
        	        break;
               	   }	
                }
    	     }
    }
    // Write the final image to disk
    image.writeBitmapFile(outImage.data());
    
    std::cout << "TEST " << (match ? "PASSED" : "FAILED") << std::endl; 
    return (match ? EXIT_SUCCESS : EXIT_FAILURE);
}
