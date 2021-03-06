
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#define _DEBUGx
#define MEM_SIZE (7524*sizeof(float))

#define MAX_SOURCE_SIZE (0x100000)


#include "precomp.hpp"
#include <stdarg.h>
#include <ctype.h>
using namespace std;

#include <sys/time.h>
#include <fstream>

extern "C"{
#include "hsa.h"
#include "hsa_ext_finalize.h"
#include "common/elf_utils.h"
};



namespace cv { namespace hsaml {


#define CHECK(msg, status) \
if (status != HSA_STATUS_SUCCESS) { \
    printf("%s failed.\n", #msg); \
    exit(1); \
} else { \
   printf("%s succeeded.\n", #msg); \
}

#define GRID_SIZE_X 1024*1024
#define GROUP_SIZE_X 256


typedef uint32_t BrigCodeOffset32_t;

typedef uint32_t BrigDataOffset32_t;

typedef uint16_t BrigKinds16_t;

typedef uint8_t BrigLinkage8_t;

typedef uint8_t BrigExecutableModifier8_t;

typedef BrigDataOffset32_t BrigDataOffsetString32_t;

enum BrigKinds {
    BRIG_KIND_NONE = 0x0000,
    BRIG_KIND_DIRECTIVE_BEGIN = 0x1000,
    BRIG_KIND_DIRECTIVE_KERNEL = 0x1008,
};

typedef struct BrigBase BrigBase;
struct BrigBase {
    uint16_t byteCount;
    BrigKinds16_t kind;
};

typedef struct BrigExecutableModifier BrigExecutableModifier;
struct BrigExecutableModifier {
    BrigExecutableModifier8_t allBits;
};

typedef struct BrigDirectiveExecutable BrigDirectiveExecutable;
struct BrigDirectiveExecutable {
    uint16_t byteCount;
    BrigKinds16_t kind;
    BrigDataOffsetString32_t name;
    uint16_t outArgCount;
    uint16_t inArgCount;
    BrigCodeOffset32_t firstInArg;
    BrigCodeOffset32_t firstCodeBlockEntry;
    BrigCodeOffset32_t nextModuleEntry;
    uint32_t codeBlockEntryCount;
    BrigExecutableModifier modifier;
    BrigLinkage8_t linkage;
    uint16_t reserved;
};

typedef struct BrigData BrigData;
struct BrigData {
    uint32_t byteCount;
    uint8_t bytes[1];
};

/*
 * Determines if the given agent is of type HSA_DEVICE_TYPE_GPU
 * and sets the value of data to the agent handle if it is.
 */
static hsa_status_t find_gpu(hsa_agent_t agent, void *data) {
    if (data == NULL) {
        return HSA_STATUS_ERROR_INVALID_ARGUMENT;
    }
    hsa_device_type_t device_type;
    hsa_status_t stat =
    hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
    if (stat != HSA_STATUS_SUCCESS) {
        return stat;
    }
    if (device_type == HSA_DEVICE_TYPE_GPU) {
        *((hsa_agent_t *)data) = agent;
    }
    return HSA_STATUS_SUCCESS;
}

/*
 * Determines if a memory region can be used for kernarg
 * allocations.
 */
static hsa_status_t get_kernarg(hsa_region_t region, void* data) {
    hsa_region_flag_t flags;
    hsa_region_get_info(region, HSA_REGION_INFO_FLAGS, &flags);
    if (flags & HSA_REGION_FLAG_KERNARG) {
        hsa_region_t* ret = (hsa_region_t*) data;
        *ret = region;
        return HSA_STATUS_SUCCESS;
    }
    return HSA_STATUS_SUCCESS;
}

/*
 * Finds the specified symbols offset in the specified brig_module.
 * If the symbol is found the function returns HSA_STATUS_SUCCESS, 
 * otherwise it returns HSA_STATUS_ERROR.
 */
hsa_status_t find_symbol_offset(hsa_ext_brig_module_t* brig_module, 
    char* symbol_name,
    hsa_ext_brig_code_section_offset32_t* offset) {
    
    /* 
     * Get the data section 
     */
    hsa_ext_brig_section_header_t* data_section_header = 
                brig_module->section[HSA_EXT_BRIG_SECTION_DATA];
    /* 
     * Get the code section
     */
    hsa_ext_brig_section_header_t* code_section_header =
             brig_module->section[HSA_EXT_BRIG_SECTION_CODE];

    /* 
     * First entry into the BRIG code section
     */
    BrigCodeOffset32_t code_offset = code_section_header->header_byte_count;
    BrigBase* code_entry = (BrigBase*) ((char*)code_section_header + code_offset);
    while (code_offset != code_section_header->byte_count) {
        if (code_entry->kind == BRIG_KIND_DIRECTIVE_KERNEL) {
            /* 
             * Now find the data in the data section
             */
            BrigDirectiveExecutable* directive_kernel = (BrigDirectiveExecutable*) (code_entry);
            BrigDataOffsetString32_t data_name_offset = directive_kernel->name;
            BrigData* data_entry = (BrigData*)((char*) data_section_header + data_name_offset);
            if (!strncmp(symbol_name, (char*)data_entry->bytes, strlen(symbol_name))){
                *offset = code_offset;
                return HSA_STATUS_SUCCESS;
            }
        }
        code_offset += code_entry->byteCount;
        code_entry = (BrigBase*) ((char*)code_section_header + code_offset);
    }
    return HSA_STATUS_ERROR;
}



typedef float Qfloat;
const int QFLOAT_TYPE = DataDepth<Qfloat>::value;


const char *getErrorString(cl_int error)
{
switch(error){
    // run-time and JIT compiler errors
    case 0: return "CL_SUCCESS";
    case -1: return "CL_DEVICE_NOT_FOUND";
    case -2: return "CL_DEVICE_NOT_AVAILABLE";
    case -3: return "CL_COMPILER_NOT_AVAILABLE";
    case -4: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case -5: return "CL_OUT_OF_RESOURCES";
    case -6: return "CL_OUT_OF_HOST_MEMORY";
    case -7: return "CL_PROFILING_INFO_NOT_AVAILABLE";
    case -8: return "CL_MEM_COPY_OVERLAP";
    case -9: return "CL_IMAGE_FORMAT_MISMATCH";
    case -10: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case -11: return "CL_BUILD_PROGRAM_FAILURE";
    case -12: return "CL_MAP_FAILURE";
    case -13: return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
    case -14: return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
    case -15: return "CL_COMPILE_PROGRAM_FAILURE";
    case -16: return "CL_LINKER_NOT_AVAILABLE";
    case -17: return "CL_LINK_PROGRAM_FAILURE";
    case -18: return "CL_DEVICE_PARTITION_FAILED";
    case -19: return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";

    // compile-time errors
    case -30: return "CL_INVALID_VALUE";
    case -31: return "CL_INVALID_DEVICE_TYPE";
    case -32: return "CL_INVALID_PLATFORM";
    case -33: return "CL_INVALID_DEVICE";
    case -34: return "CL_INVALID_CONTEXT";
    case -35: return "CL_INVALID_QUEUE_PROPERTIES";
    case -36: return "CL_INVALID_COMMAND_QUEUE";
    case -37: return "CL_INVALID_HOST_PTR";
    case -38: return "CL_INVALID_MEM_OBJECT";
    case -39: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
    case -40: return "CL_INVALID_IMAGE_SIZE";
    case -41: return "CL_INVALID_SAMPLER";
    case -42: return "CL_INVALID_BINARY";
    case -43: return "CL_INVALID_BUILD_OPTIONS";
    case -44: return "CL_INVALID_PROGRAM";
    case -45: return "CL_INVALID_PROGRAM_EXECUTABLE";
    case -46: return "CL_INVALID_KERNEL_NAME";
    case -47: return "CL_INVALID_KERNEL_DEFINITION";
    case -48: return "CL_INVALID_KERNEL";
    case -49: return "CL_INVALID_ARG_INDEX";
    case -50: return "CL_INVALID_ARG_VALUE";
    case -51: return "CL_INVALID_ARG_SIZE";
    case -52: return "CL_INVALID_KERNEL_ARGS";
    case -53: return "CL_INVALID_WORK_DIMENSION";
    case -54: return "CL_INVALID_WORK_GROUP_SIZE";
    case -55: return "CL_INVALID_WORK_ITEM_SIZE";
    case -56: return "CL_INVALID_GLOBAL_OFFSET";
    case -57: return "CL_INVALID_EVENT_WAIT_LIST";
    case -58: return "CL_INVALID_EVENT";
    case -59: return "CL_INVALID_OPERATION";
    case -60: return "CL_INVALID_GL_OBJECT";
    case -61: return "CL_INVALID_BUFFER_SIZE";
    case -62: return "CL_INVALID_MIP_LEVEL";
    case -63: return "CL_INVALID_GLOBAL_WORK_SIZE";
    case -64: return "CL_INVALID_PROPERTY";
    case -65: return "CL_INVALID_IMAGE_DESCRIPTOR";
    case -66: return "CL_INVALID_COMPILER_OPTIONS";
    case -67: return "CL_INVALID_LINKER_OPTIONS";
    case -68: return "CL_INVALID_DEVICE_PARTITION_COUNT";

    // extension errors
    case -1000: return "CL_INVALID_GL_SHAREGROUP_REFERENCE_KHR";
    case -1001: return "CL_PLATFORM_NOT_FOUND_KHR";
    case -1002: return "CL_INVALID_D3D10_DEVICE_KHR";
    case -1003: return "CL_INVALID_D3D10_RESOURCE_KHR";
    case -1004: return "CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR";
    case -1005: return "CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR";
    default: return "Unknown OpenCL error";
    }
}


// Param Grid
static void checkParamGrid(const ParamGrid& pg)
{
    if( pg.minVal > pg.maxVal )
        CV_Error( CV_StsBadArg, "Lower bound of the grid must be less then the upper one" );
    if( pg.minVal < DBL_EPSILON )
        CV_Error( CV_StsBadArg, "Lower bound of the grid must be positive" );
    if( pg.logStep < 1. + FLT_EPSILON )
        CV_Error( CV_StsBadArg, "Grid step must greater then 1" );
}

// SVM training parameters
SVM::Params::Params()
{
    svmType = SVM::C_SVC;
    kernelType = SVM::RBF;
    degree = 0;


    gamma = 1;
    coef0 = 0;
    C = 1;
    nu = 0;
    p = 0;
    termCrit = TermCriteria( CV_TERMCRIT_ITER+CV_TERMCRIT_EPS, 1000, FLT_EPSILON );
}


SVM::Params::Params( int _svmType, int _kernelType,
                     double _degree, double _gamma, double _coef0,
                     double _Con, double _nu, double _p,
                     const Mat& _classWeights, TermCriteria _termCrit )
{
    svmType = _svmType;
    kernelType = _kernelType;
    degree = _degree;
    gamma = _gamma;
    coef0 = _coef0;
    C = _Con;
    nu = _nu;
    p = _p;
    classWeights = _classWeights;
    termCrit = _termCrit;

}

/////////////////////////////////////// SVM kernel ///////////////////////////////////////
class SVMKernelImpl : public SVM::Kernel
{


	int n_points=MEM_SIZE;
    	int n_dim=1;
	float gamma;


	FILE *fp;
	String fileName;
	char *source_str;
	size_t source_size;
	bool bKernelInit=false;

	const int id_desired=0;

	fstream f_perm_svm;
	timeval t1, t2;
	double elapsedTime;

	//HSA Variables
	hsa_status_t err;
	hsa_agent_t device = 0;
	uint32_t queue_size = 0;
	hsa_queue_t* commandQueue;
	hsa_ext_brig_module_t* brigModule;
	hsa_ext_program_handle_t hsaProgram;
	hsa_ext_brig_module_handle_t module;
	hsa_ext_finalization_request_t finalization_request_list;
 	hsa_ext_code_descriptor_t *hsaCodeDescriptor;
	hsa_signal_t signal;
	hsa_dispatch_packet_t aql;
 	hsa_region_t kernarg_region = 0;
	void* kernel_arg_buffer = NULL;
 	void* kernel_arg_buffer_backup[1500] = {NULL}; 
	size_t kernel_arg_buffer_size;
	uint64_t kernel_arg_start_offset = 0;
 	void *kernel_arg_buffer_start;
	int svm_iter=0;
	

public:
    SVMKernelImpl()
    {
	
    }

    ~SVMKernelImpl(){
    	if(bKernelInit){
		cout<<"SVMKernelImpl deconstruct!"<<endl;
		err=hsa_signal_destroy(signal);
    		CHECK(Destroying the signal, err);

    		err=hsa_ext_program_destroy(hsaProgram);
    		CHECK(Destroying the program, err);

   		 destroy_brig_module(brigModule);

    		err=hsa_queue_destroy(commandQueue);
    		CHECK(Destroying the queue, err);
    
		err=hsa_shut_down();
    		CHECK(Shutting down the runtime, err);
		bKernelInit=false;
    	}


    }

    SVMKernelImpl( const SVM::Params& _params )
    {
        params = _params;
        cout<<"svm kernel impl construct"<<endl;

        f_perm_svm.open("perf_svm.txt", ios::out );
        f_perm_svm.close();

    	if(bKernelInit){
    		cout<<"encounter illegal clmem "<<endl;
    		bKernelInit=false;
    		exit(1);
    	}



	err = hsa_init();
	CHECK(Initializing the hsa runtime, err);

    	// Iterate over the agents and pick the gpu agent using 
     	// the find_gpu callback.
     	
    
    	err = hsa_iterate_agents(find_gpu, &device);
    	CHECK(Calling hsa_iterate_agents, err);

    	err = (device == 0) ? HSA_STATUS_ERROR : HSA_STATUS_SUCCESS;
    	CHECK(Checking if the GPU device is non-zero, err);

    	
     	// Query the name of the device.
     	
    
	char name[64] = { 0 };
    	err = hsa_agent_get_info(device, HSA_AGENT_INFO_NAME, name);
    	CHECK(Querying the device name, err);
    	printf("The device name is %s.\n", name);

    	
     	// Query the maximum size of the queue.
     	
    
    	err = hsa_agent_get_info(device, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &queue_size);
    	CHECK(Querying the device maximum queue size, err);
    	printf("The maximum queue size is %u.\n", (unsigned int) queue_size);

     	//Create a queue using the maximum size.
    	err = hsa_queue_create(device, queue_size, HSA_QUEUE_TYPE_MULTI, NULL, NULL, &commandQueue);
    	CHECK(Creating the queue, err);

    	
     	//Load BRIG, encapsulated in an ELF container, into a BRIG module.
     
    	brigModule;
    	char file_name[128] = "hogsvm.brig";
    	err = (hsa_status_t) create_brig_module_from_brig_file(file_name, &brigModule);
    	CHECK(Creating the brig module from hogsvm.brig, err);

    	
     	//Create hsa program.
    	err = hsa_ext_program_create(&device, 1, HSA_EXT_BRIG_MACHINE_LARGE, HSA_EXT_BRIG_PROFILE_FULL, &hsaProgram);
    	CHECK(Creating the hsa program, err);

    
     	//Add the BRIG module to hsa program.
     
    	err = hsa_ext_add_module(hsaProgram, brigModule, &module);
    	CHECK(Adding the brig module to the program, err);

    
     	//Construct finalization request list.
    	finalization_request_list.module = module;
    	finalization_request_list.program_call_convention = 0;
    	char kernel_name[128] = "&__OpenCL_svmlinear_kernel";
    	err = find_symbol_offset(brigModule, kernel_name, &finalization_request_list.symbol);
    	CHECK(Finding the symbol offset for the kernel, err);

    
     	//Finalize the hsa program.
     	err = hsa_ext_finalize_program(hsaProgram, device, 1, &finalization_request_list, NULL, NULL, 0, NULL, 0);
    	CHECK(Finalizing the program, err);

    
     	//Get the hsa code descriptor address.
    
    	err = hsa_ext_query_kernel_descriptor_address(hsaProgram, module, finalization_request_list.symbol, &hsaCodeDescriptor);
    	CHECK(Querying the kernel descriptor address, err);

    
  	memset(&aql, 0, sizeof(aql));



    	 cout<<"end of svm kernel impl construct"<<endl;



    }



    int getType() const
    {
        return params.kernelType;
    }

    void calc_non_rbf_base1( int vcount, int var_count, const float* vecs,
                                const float* another, Qfloat* results,
                                double alpha, double beta )
    {
        int static count=0;
#ifdef _DEBUG
        cout<<"rbf :"<<count++<<endl;
#endif
            int j, k;

            const float* sample=NULL;
            for( j = 0; j < vcount; j++ )
            {
            	double s = 0;
            	sample = &vecs[j*var_count];
                for( k = 0; k <= var_count - 4; k += 4 )
                    s += sample[k]*another[k] + sample[k+1]*another[k+1] +
                    sample[k+2]*another[k+2] + sample[k+3]*another[k+3];
                for( ; k < var_count; k++ )
                    s += sample[k]*another[k];
                results[j] = (Qfloat)(s*alpha + beta);


            }

#ifdef _DEBUG
			for(int k=0;k<var_count;k++){
				printf("vecs[%d]=%f\n",k,vecs[var_count*(vcount-1)+k]);
			}
			printf("non-parallel Finished in count%d\n",count);
			printf("results[%d]=%f\n",vcount-1,results[vcount-1]);
			cout<<"a b sample0 another0 = "<<alpha<<"  "<<beta<<" "<<" "<<sample[0]<<" "<<another[0]<<" "<<endl;
			getchar();
#endif
			//results=134.526535
			//NDRange Finished in count3
			//results[3633]=134.526535


    }


    void calc_non_rbf_base( int vcount, int var_count, const float* vecs,
                                const float* another, Qfloat* results,
                                double alpha, double beta )
    {

    	cl_uint vcount2=(cl_uint)vcount;
    	cl_uint var_count2 = (cl_uint)var_count;
	float alpha2=(float)alpha;
	float beta2=(float)beta;
	cl_uint bid=0;
	float *sum_partial=new float[var_count];
	double sum_dotprod=0;
    	static int count=0;
#ifdef _DEBUG
    	cout<<"count of call = "<<(count++)<<endl;
#endif

#ifdef _DEBUG
	gettimeofday(&t1, NULL);
#endif
	//bKernelInit=false;
	err=hsa_signal_create(1, 0, NULL, &signal);
	aql.completion_signal=signal;

	if(!bKernelInit){
    
		bKernelInit=true;
	
				
		aql.dimensions=1;
    		aql.workgroup_size_x=1;
    		aql.workgroup_size_y=1;
    		aql.workgroup_size_z=1;
    		aql.grid_size_x=vcount;
    		aql.grid_size_y=1;
    		aql.grid_size_z=1;
    		aql.header.type=HSA_PACKET_TYPE_DISPATCH;
    		aql.header.acquire_fence_scope=2;
    		aql.header.release_fence_scope=2;
    		aql.header.barrier=1;
    		aql.group_segment_size= hsaCodeDescriptor->workgroup_group_segment_byte_size;
    		aql.private_segment_size= hsaCodeDescriptor->workitem_private_segment_byte_size;
		aql.kernel_object_address=hsaCodeDescriptor->code.handle;

		//ret = clSetKernelArg(kernel, 0, sizeof(cl_mem),   (void *)&cm_samples);
		//ret = clSetKernelArg(kernel, 1, sizeof(cl_mem),   (void *)&cm_another);
		//ret = clSetKernelArg(kernel, 2, sizeof(cl_uint),  (void *)&vcount2);
		//ret = clSetKernelArg(kernel, 3, sizeof(cl_uint),  (void *)&var_count2);
		//ret = clSetKernelArg(kernel, 4, sizeof(cl_float), (void *)&alpha2);
		//ret = clSetKernelArg(kernel, 5, sizeof(cl_float), (void *)&beta2);
		//ret = clSetKernelArg(kernel, 6, sizeof(cl_mem),   (void *)&cm_results);
	
		#ifdef _DEBUG
		//printf("NDRange Finished in count%d\n",count);
		//printf("results[%d]=%f\n",vcount-1,results[vcount-1]);
		//getchar();
		#endif
    		
		    		// Setup the dispatch information.
    	
    		// Allocate and initialize the kernel arguments.
     
    		err=hsa_memory_register(vecs, vcount*var_count*sizeof(float) );
    	//	CHECK(Registering argument memory for vecs parameter, err);
    
    		err=hsa_memory_register(another, var_count*sizeof(float) );
    	//	CHECK(Registering argument memory for another parameter, err);
    
		err=hsa_memory_register(&vcount2, sizeof(int) );
    	//	CHECK(Registering argument memory for vcount  parameter, err);
    
		err=hsa_memory_register(&var_count2, sizeof(int) );
    	//	CHECK(Registering argument memory for var_count parameter, err);
    
		err=hsa_memory_register(&alpha2, sizeof(float) );
    	//	CHECK(Registering argument memory for alpha parameter, err);
    
		err=hsa_memory_register(&beta2, sizeof(float) );
    	//	CHECK(Registering argument memory for beta  parameter, err);
    
		err=hsa_memory_register(results, vcount*sizeof(float) );
    	///	CHECK(Registering argument memory for result parameter, err);
	
	//Find a memory region that supports kernel arguments.
    		//cout<<"start to alloc kernel region"<<endl;
		kernarg_region = 0;
    		hsa_agent_iterate_regions(device, get_kernarg, &kernarg_region);
    		err = (kernarg_region == 0) ? HSA_STATUS_ERROR : HSA_STATUS_SUCCESS;
    	//	CHECK(Finding a kernarg memory region, err);
	
	
    		kernel_arg_buffer = NULL;

    		kernel_arg_buffer_size = hsaCodeDescriptor->kernarg_segment_byte_size;
 	
		// Allocate the kernel argument buffer from the correct region.
     
    		err = hsa_memory_allocate(kernarg_region, kernel_arg_buffer_size, &kernel_arg_buffer);
    		//err = hsa_memory_allocate(kernarg_region, kernel_arg_buffer_size, &kernel_arg_buffer_backup);
		CHECK(Allocating kernel argument memory buffer, err);

	}	
     	    	kernel_arg_start_offset = 0;
    		//This flags should be set if HSA_HLC_Stable is used
    		// This is because the high level compiler generates 6 extra args
    		kernel_arg_start_offset += sizeof(uint64_t) * 6;
    		//printf("Using dummy args \n");
    		memset(kernel_arg_buffer, 0, kernel_arg_buffer_size);
    		kernel_arg_buffer_start = (char*)kernel_arg_buffer + kernel_arg_start_offset;
		int offset_cur=0;
    		memcpy(kernel_arg_buffer_start,                   &vecs,                 sizeof(void*));
		offset_cur += sizeof(void*);
    		memcpy(kernel_arg_buffer_start + offset_cur,   &another,              sizeof(void*));
 		offset_cur += sizeof(void*);
		memcpy(kernel_arg_buffer_start + offset_cur, &vcount,    sizeof(int));
                offset_cur += sizeof(int);
		memcpy(kernel_arg_buffer_start + offset_cur, &var_count, sizeof(int));
    		offset_cur += sizeof(int);
		memcpy(kernel_arg_buffer_start + offset_cur, &alpha2,    sizeof(float));
                offset_cur += sizeof(float);
		memcpy(kernel_arg_buffer_start + offset_cur, &beta2,     sizeof(float));
		offset_cur += sizeof(float);
		memcpy(kernel_arg_buffer_start + offset_cur, &results,   sizeof(void*));
 	
    	
   	   	
//	printf("kernel_arg_buffer_size=%d\n",kernel_arg_buffer_size);
	//err = hsa_memory_allocate(kernarg_region, kernel_arg_buffer_size, &kernel_arg_buffer);

	//memcpy(kernel_arg_buffer,kernel_arg_buffer_backup,kernel_arg_buffer_size); 
	

		aql.kernarg_address=(uint64_t)kernel_arg_buffer;
		//Obtain the current queue write index 
    		uint64_t index = hsa_queue_load_write_index_relaxed(commandQueue);

    		
     		// Write the aql packet at the calculated queue index address.
     		
    		const uint32_t queueMask = commandQueue->size - 1;
    		((hsa_dispatch_packet_t*)(commandQueue->base_address))[index&queueMask]=aql;

    
    		// Increment the write index and ring the doorbell to dispatch the kernel.
     		hsa_queue_store_write_index_relaxed(commandQueue, index+1);
    		hsa_signal_store_relaxed(commandQueue->doorbell_signal, index);
    		//CHECK(Dispatching the kernel, err);

    
    		//Wait on the dispatch signal until the kernel is finished.
   		err = hsa_signal_wait_acquire(signal, HSA_LT, 1, (uint64_t) -1, HSA_WAIT_EXPECTANCY_UNKNOWN);
    		//CHECK(Wating on the dispatch signal, err);
		
 	#ifdef _DEBUG
	for(int i=0;i<5;i++){
		printf("vecs[%d]=%f  results[%d]=%f\n",i*var_count,vecs[i*var_count],i,results[i]);
	}
    	gettimeofday(&t2, NULL);
    	elapsedTime = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0;
	cout <<"one loop cost : "<< elapsedTime << " s.\n";
    	cout<<"Press any key to continue"<<endl;
    	getchar();
	#endif

    }



    void calc_linear( int vcount, int var_count, const float* vecs,
                      const float* another, Qfloat* results )
    {
        calc_non_rbf_base( vcount, var_count, vecs, another, results, 1, 0 );
    }

    void calc_poly( int vcount, int var_count, const float* vecs,
                    const float* another, Qfloat* results )
    {
        Mat R( 1, vcount, QFLOAT_TYPE, results );
        calc_non_rbf_base( vcount, var_count, vecs, another, results, params.gamma, params.coef0 );
        if( vcount > 0 )
            pow( R, params.degree, R );
    }

    void calc_sigmoid( int vcount, int var_count, const float* vecs,
                       const float* another, Qfloat* results )
    {
        int j;
        calc_non_rbf_base( vcount, var_count, vecs, another, results,
                          -2*params.gamma, -2*params.coef0 );
        // TODO: speedup this
        for( j = 0; j < vcount; j++ )
        {
            Qfloat t = results[j];
            Qfloat e = std::exp(-std::abs(t));
            if( t > 0 )
                results[j] = (Qfloat)((1. - e)/(1. + e));
            else
                results[j] = (Qfloat)((e - 1.)/(e + 1.));
        }
    }


    void calc_rbf( int vcount, int var_count, const float* vecs,
                   const float* another, Qfloat* results )
    {
        double gamma = -params.gamma;
        int j, k;

        for( j = 0; j < vcount; j++ )
        {
            const float* sample = &vecs[j*var_count];
            double s = 0;

            for( k = 0; k <= var_count - 4; k += 4 )
            {
                double t0 = sample[k] - another[k] ;
                double t1 = sample[k+1] - another[k+1] ;

                s += t0*t0 + t1*t1;

                t0 = sample[k+2] - another[k+2];
                t1 = sample[k+3] - another[k+3];

                s += t0*t0 + t1*t1;
            }

            for( ; k < var_count; k++ )
            {
                double t0 = sample[k] - another[k];
                s += t0 * t0 ;
            }
            results[j] = (Qfloat)(s*gamma);
        }

        if( vcount > 0 )
        {
            Mat R( 1, vcount, QFLOAT_TYPE, results );
            exp( R, R );
        }
    }

    /// Histogram intersection kernel
    void calc_intersec( int vcount, int var_count, const float* vecs,
                        const float* another, Qfloat* results )
    {
        int j, k;
        for( j = 0; j < vcount; j++ )
        {
            const float* sample = &vecs[j*var_count];
            double s = 0;
            for( k = 0; k <= var_count - 4; k += 4 )
                s += std::min(sample[k],another[k]) + std::min(sample[k+1],another[k+1]) +
                std::min(sample[k+2],another[k+2]) + std::min(sample[k+3],another[k+3]);
            for( ; k < var_count; k++ )
                s += std::min(sample[k],another[k]);
            results[j] = (Qfloat)(s);
        }
    }

    /// Exponential chi2 kernel
    void calc_chi2( int vcount, int var_count, const float* vecs,
                    const float* another, Qfloat* results )
    {
        Mat R( 1, vcount, QFLOAT_TYPE, results );
        double gamma = -params.gamma;
        int j, k;
        for( j = 0; j < vcount; j++ )
        {
            const float* sample = &vecs[j*var_count];
            double chi2 = 0;
            for(k = 0 ; k < var_count; k++ )
            {
                double d = sample[k]-another[k];
                double devisor = sample[k]+another[k];
                /// if devisor == 0, the Chi2 distance would be zero,
                // but calculation would rise an error because of deviding by zero
                if (devisor != 0)
                {
                    chi2 += d*d/devisor;
                }
            }
            results[j] = (Qfloat) (gamma*chi2);
        }
        if( vcount > 0 )
            exp( R, R );
    }

    void calc( int vcount, int var_count, const float* vecs,
               const float* another, Qfloat* results )
    {
        switch( params.kernelType )
        {
        case SVM::LINEAR:
            calc_linear(vcount, var_count, vecs, another, results);
            break;
        case SVM::RBF:
            calc_rbf(vcount, var_count, vecs, another, results);
            break;
        case SVM::POLY:
            calc_poly(vcount, var_count, vecs, another, results);
            break;
        case SVM::SIGMOID:
            calc_sigmoid(vcount, var_count, vecs, another, results);
            break;
        case SVM::CHI2:
            calc_chi2(vcount, var_count, vecs, another, results);
            break;
        case SVM::INTER:
            calc_intersec(vcount, var_count, vecs, another, results);
            break;
        default:
            CV_Error(CV_StsBadArg, "Unknown kernel type");
        }
        const Qfloat max_val = (Qfloat)(FLT_MAX*1e-3);
        for( int j = 0; j < vcount; j++ )
        {
            if( results[j] > max_val )
                results[j] = max_val;
        }
    }

    SVM::Params params;
};



/////////////////////////////////////////////////////////////////////////

static void sortSamplesByClasses( const Mat& _samples, const Mat& _responses,
                           vector<int>& sidx_all, vector<int>& class_ranges )
{
    int i, nsamples = _samples.rows;
    CV_Assert( _responses.isContinuous() && _responses.checkVector(1, CV_32S) == nsamples );

    setRangeVector(sidx_all, nsamples);

    const int* rptr = _responses.ptr<int>();
    std::sort(sidx_all.begin(), sidx_all.end(), cmp_lt_idx<int>(rptr));
    class_ranges.clear();
    class_ranges.push_back(0);

    for( i = 0; i < nsamples; i++ )
    {
        if( i == nsamples-1 || rptr[sidx_all[i]] != rptr[sidx_all[i+1]] )
            class_ranges.push_back(i+1);
    }
}

//////////////////////// SVM implementation //////////////////////////////

ParamGrid SVM::getDefaultGrid( int param_id )
{
    ParamGrid grid;
    if( param_id == SVM::C )
    {
        grid.minVal = 0.1;
        grid.maxVal = 500;
        grid.logStep = 5; // total iterations = 5
    }
    else if( param_id == SVM::GAMMA )
    {
        grid.minVal = 1e-5;
        grid.maxVal = 0.6;
        grid.logStep = 15; // total iterations = 4
    }
    else if( param_id == SVM::P )
    {
        grid.minVal = 0.01;
        grid.maxVal = 100;
        grid.logStep = 7; // total iterations = 4
    }
    else if( param_id == SVM::NU )
    {
        grid.minVal = 0.01;
        grid.maxVal = 0.2;
        grid.logStep = 3; // total iterations = 3
    }
    else if( param_id == SVM::COEF )
    {
        grid.minVal = 0.1;
        grid.maxVal = 300;
        grid.logStep = 14; // total iterations = 3
    }
    else if( param_id == SVM::DEGREE )
    {
        grid.minVal = 0.01;
        grid.maxVal = 4;
        grid.logStep = 7; // total iterations = 3
    }
    else
        cvError( CV_StsBadArg, "SVM::getDefaultGrid", "Invalid type of parameter "
                "(use one of SVM::C, SVM::GAMMA et al.)", __FILE__, __LINE__ );
    return grid;
}


class SVMImpl : public SVM
{
public:
    struct DecisionFunc
    {
        DecisionFunc(double _rho, int _ofs) : rho(_rho), ofs(_ofs) {}
        DecisionFunc() : rho(0.), ofs(0) {}
        double rho;
        int ofs;
    };

    // Generalized SMO+SVMlight algorithm
    // Solves:
    //
    //  min [0.5(\alpha^T Q \alpha) + b^T \alpha]
    //
    //      y^T \alpha = \delta
    //      y_i = +1 or -1
    //      0 <= alpha_i <= Cp for y_i = 1
    //      0 <= alpha_i <= Cn for y_i = -1
    //
    // Given:
    //
    //  Q, b, y, Cp, Cn, and an initial feasible point \alpha
    //  l is the size of vectors and matrices
    //  eps is the stopping criterion
    //
    // solution will be put in \alpha, objective value will be put in obj
    //
    class Solver
    {
    public:
        enum { MIN_CACHE_SIZE = (40 << 20) /* 40Mb */, MAX_CACHE_SIZE = (500 << 20) /* 500Mb */ };

        typedef bool (Solver::*SelectWorkingSet)( int& i, int& j );
        typedef Qfloat* (Solver::*GetRow)( int i, Qfloat* row, Qfloat* dst, bool existed );
        typedef void (Solver::*CalcRho)( double& rho, double& r );

        struct KernelRow
        {
            KernelRow() { idx = -1; prev = next = 0; }
            KernelRow(int _idx, int _prev, int _next) : idx(_idx), prev(_prev), next(_next) {}
            int idx;
            int prev;
            int next;
        };

        struct SolutionInfo
        {
            SolutionInfo() { obj = rho = upper_bound_p = upper_bound_n = r = 0; }
            double obj;
            double rho;
            double upper_bound_p;
            double upper_bound_n;
            double r;   // for Solver_NU
        };

        void clear()
        {
            alpha_vec = 0;
            select_working_set_func = 0;
            calc_rho_func = 0;
            get_row_func = 0;
            lru_cache.clear();
        }

        Solver( const Mat& _samples, const vector<schar>& _y,
                vector<double>& _alpha, const vector<double>& _b,
                double _Cp, double _Cn,
                const Ptr<SVM::Kernel>& _kernel, GetRow _get_row,
                SelectWorkingSet _select_working_set, CalcRho _calc_rho,
                TermCriteria _termCrit )
        {
            clear();

            samples = _samples;
            sample_count = samples.rows;
            var_count = samples.cols;

            y_vec = _y;
            alpha_vec = &_alpha;
            alpha_count = (int)alpha_vec->size();
            b_vec = _b;
            kernel = _kernel;

            C[0] = _Cn;
            C[1] = _Cp;
            eps = _termCrit.epsilon;
            max_iter = _termCrit.maxCount;

            G_vec.resize(alpha_count);
            alpha_status_vec.resize(alpha_count);
            buf[0].resize(sample_count*2);
            buf[1].resize(sample_count*2);

            select_working_set_func = _select_working_set;
            CV_Assert(select_working_set_func != 0);

            calc_rho_func = _calc_rho;
            CV_Assert(calc_rho_func != 0);

            get_row_func = _get_row;
            CV_Assert(get_row_func != 0);

            // assume that for large training sets ~25% of Q matrix is used
            int64 csize = (int64)sample_count*sample_count/4;
            csize = std::max(csize, (int64)(MIN_CACHE_SIZE/sizeof(Qfloat)) );
            csize = std::min(csize, (int64)(MAX_CACHE_SIZE/sizeof(Qfloat)) );
            max_cache_size = (int)((csize + sample_count-1)/sample_count);
            max_cache_size = std::min(std::max(max_cache_size, 1), sample_count);
            cache_size = 0;

            lru_cache.clear();
            lru_cache.resize(sample_count+1, KernelRow(-1, 0, 0));
            lru_first = lru_last = 0;
            lru_cache_data.create(max_cache_size, sample_count, QFLOAT_TYPE);
        }

        Qfloat* get_row_base( int i, bool* _existed )
        {
            int i1 = i < sample_count ? i : i - sample_count;
            KernelRow& kr = lru_cache[i1+1];
            if( _existed )
                *_existed = kr.idx >= 0;
            if( kr.idx < 0 )
            {
                if( cache_size < max_cache_size )
                {
                    kr.idx = cache_size;
                    cache_size++;
                }
                else
                {
                    KernelRow& last = lru_cache[lru_last];
                    kr.idx = last.idx;
                    last.idx = -1;
                    lru_cache[last.prev].next = 0;
                    lru_last = last.prev;
                }
                kernel->calc( sample_count, var_count, samples.ptr<float>(),
                              samples.ptr<float>(i1), lru_cache_data.ptr<Qfloat>(kr.idx) );
            }
            else
            {
                if( kr.next )
                    lru_cache[kr.next].prev = kr.prev;
                else
                    lru_last = kr.prev;
                if( kr.prev )
                    lru_cache[kr.prev].next = kr.next;
                else
                    lru_first = kr.next;
            }
            kr.next = lru_first;
            kr.prev = 0;
            lru_first = i1+1;

            return lru_cache_data.ptr<Qfloat>(kr.idx);
        }

        Qfloat* get_row_svc( int i, Qfloat* row, Qfloat*, bool existed )
        {
            if( !existed )
            {
                const schar* _y = &y_vec[0];
                int j, len = sample_count;

                if( _y[i] > 0 )
                {
                    for( j = 0; j < len; j++ )
                        row[j] = _y[j]*row[j];
                }
                else
                {
                    for( j = 0; j < len; j++ )
                        row[j] = -_y[j]*row[j];
                }
            }
            return row;
        }

        Qfloat* get_row_one_class( int, Qfloat* row, Qfloat*, bool )
        {
            return row;
        }

        Qfloat* get_row_svr( int i, Qfloat* row, Qfloat* dst, bool )
        {
            int j, len = sample_count;
            Qfloat* dst_pos = dst;
            Qfloat* dst_neg = dst + len;
            if( i >= len )
                std::swap(dst_pos, dst_neg);

            for( j = 0; j < len; j++ )
            {
                Qfloat t = row[j];
                dst_pos[j] = t;
                dst_neg[j] = -t;
            }
            return dst;
        }

        Qfloat* get_row( int i, float* dst )
        {
            bool existed = false;
            float* row = get_row_base( i, &existed );
            return (this->*get_row_func)( i, row, dst, existed );
        }

        #undef is_upper_bound
        #define is_upper_bound(i) (alpha_status[i] > 0)

        #undef is_lower_bound
        #define is_lower_bound(i) (alpha_status[i] < 0)

        #undef is_free
        #define is_free(i) (alpha_status[i] == 0)

        #undef get_C
        #define get_C(i) (C[y[i]>0])

        #undef update_alpha_status
        #define update_alpha_status(i) \
            alpha_status[i] = (schar)(alpha[i] >= get_C(i) ? 1 : alpha[i] <= 0 ? -1 : 0)

        #undef reconstruct_gradient
        #define reconstruct_gradient() /* empty for now */

        bool solve_generic( SolutionInfo& si )
        {
            const schar* y = &y_vec[0];
            double* alpha = &alpha_vec->at(0);
            schar* alpha_status = &alpha_status_vec[0];
            double* G = &G_vec[0];
            double* b = &b_vec[0];

            int iter = 0;
            int i, j, k;

            // 1. initialize gradient and alpha status
            for( i = 0; i < alpha_count; i++ )
            {
                update_alpha_status(i);
                G[i] = b[i];
                if( fabs(G[i]) > 1e200 )
                    return false;
            }

            for( i = 0; i < alpha_count; i++ )
            {
                if( !is_lower_bound(i) )
                {
                    const Qfloat *Q_i = get_row( i, &buf[0][0] );
                    double alpha_i = alpha[i];

                    for( j = 0; j < alpha_count; j++ )
                        G[j] += alpha_i*Q_i[j];
                }
            }

            // 2. optimization loop
            for(;;)
            {
                const Qfloat *Q_i, *Q_j;
                double C_i, C_j;
                double old_alpha_i, old_alpha_j, alpha_i, alpha_j;
                double delta_alpha_i, delta_alpha_j;

        #ifdef _DEBUG
                for( i = 0; i < alpha_count; i++ )
                {
                    if( fabs(G[i]) > 1e+300 )
                        return false;

                    if( fabs(alpha[i]) > 1e16 )
                        return false;
                }
        #endif

                if( (this->*select_working_set_func)( i, j ) != 0 || iter++ >= max_iter )
                    break;

                Q_i = get_row( i, &buf[0][0] );
                Q_j = get_row( j, &buf[1][0] );

                C_i = get_C(i);
                C_j = get_C(j);

                alpha_i = old_alpha_i = alpha[i];
                alpha_j = old_alpha_j = alpha[j];

                if( y[i] != y[j] )
                {
                    double denom = Q_i[i]+Q_j[j]+2*Q_i[j];
                    double delta = (-G[i]-G[j])/MAX(fabs(denom),FLT_EPSILON);
                    double diff = alpha_i - alpha_j;
                    alpha_i += delta;
                    alpha_j += delta;

                    if( diff > 0 && alpha_j < 0 )
                    {
                        alpha_j = 0;
                        alpha_i = diff;
                    }
                    else if( diff <= 0 && alpha_i < 0 )
                    {
                        alpha_i = 0;
                        alpha_j = -diff;
                    }

                    if( diff > C_i - C_j && alpha_i > C_i )
                    {
                        alpha_i = C_i;
                        alpha_j = C_i - diff;
                    }
                    else if( diff <= C_i - C_j && alpha_j > C_j )
                    {
                        alpha_j = C_j;
                        alpha_i = C_j + diff;
                    }
                }
                else
                {
                    double denom = Q_i[i]+Q_j[j]-2*Q_i[j];
                    double delta = (G[i]-G[j])/MAX(fabs(denom),FLT_EPSILON);
                    double sum = alpha_i + alpha_j;
                    alpha_i -= delta;
                    alpha_j += delta;

                    if( sum > C_i && alpha_i > C_i )
                    {
                        alpha_i = C_i;
                        alpha_j = sum - C_i;
                    }
                    else if( sum <= C_i && alpha_j < 0)
                    {
                        alpha_j = 0;
                        alpha_i = sum;
                    }

                    if( sum > C_j && alpha_j > C_j )
                    {
                        alpha_j = C_j;
                        alpha_i = sum - C_j;
                    }
                    else if( sum <= C_j && alpha_i < 0 )
                    {
                        alpha_i = 0;
                        alpha_j = sum;
                    }
                }

                // update alpha
                alpha[i] = alpha_i;
                alpha[j] = alpha_j;
                update_alpha_status(i);
                update_alpha_status(j);

                // update G
                delta_alpha_i = alpha_i - old_alpha_i;
                delta_alpha_j = alpha_j - old_alpha_j;

                for( k = 0; k < alpha_count; k++ )
                    G[k] += Q_i[k]*delta_alpha_i + Q_j[k]*delta_alpha_j;
            }

            // calculate rho
            (this->*calc_rho_func)( si.rho, si.r );

            // calculate objective value
            for( i = 0, si.obj = 0; i < alpha_count; i++ )
                si.obj += alpha[i] * (G[i] + b[i]);

            si.obj *= 0.5;

            si.upper_bound_p = C[1];
            si.upper_bound_n = C[0];

            return true;
        }

        // return 1 if already optimal, return 0 otherwise
        bool select_working_set( int& out_i, int& out_j )
        {
            // return i,j which maximize -grad(f)^T d , under constraint
            // if alpha_i == C, d != +1
            // if alpha_i == 0, d != -1
            double Gmax1 = -DBL_MAX;        // max { -grad(f)_i * d | y_i*d = +1 }
            int Gmax1_idx = -1;

            double Gmax2 = -DBL_MAX;        // max { -grad(f)_i * d | y_i*d = -1 }
            int Gmax2_idx = -1;

            const schar* y = &y_vec[0];
            const schar* alpha_status = &alpha_status_vec[0];
            const double* G = &G_vec[0];

            for( int i = 0; i < alpha_count; i++ )
            {
                double t;

                if( y[i] > 0 )    // y = +1
                {
                    if( !is_upper_bound(i) && (t = -G[i]) > Gmax1 )  // d = +1
                    {
                        Gmax1 = t;
                        Gmax1_idx = i;
                    }
                    if( !is_lower_bound(i) && (t = G[i]) > Gmax2 )  // d = -1
                    {
                        Gmax2 = t;
                        Gmax2_idx = i;
                    }
                }
                else        // y = -1
                {
                    if( !is_upper_bound(i) && (t = -G[i]) > Gmax2 )  // d = +1
                    {
                        Gmax2 = t;
                        Gmax2_idx = i;
                    }
                    if( !is_lower_bound(i) && (t = G[i]) > Gmax1 )  // d = -1
                    {
                        Gmax1 = t;
                        Gmax1_idx = i;
                    }
                }
            }

            out_i = Gmax1_idx;
            out_j = Gmax2_idx;

            return Gmax1 + Gmax2 < eps;
        }

        void calc_rho( double& rho, double& r )
        {
            int nr_free = 0;
            double ub = DBL_MAX, lb = -DBL_MAX, sum_free = 0;
            const schar* y = &y_vec[0];
            const schar* alpha_status = &alpha_status_vec[0];
            const double* G = &G_vec[0];

            for( int i = 0; i < alpha_count; i++ )
            {
                double yG = y[i]*G[i];

                if( is_lower_bound(i) )
                {
                    if( y[i] > 0 )
                        ub = MIN(ub,yG);
                    else
                        lb = MAX(lb,yG);
                }
                else if( is_upper_bound(i) )
                {
                    if( y[i] < 0)
                        ub = MIN(ub,yG);
                    else
                        lb = MAX(lb,yG);
                }
                else
                {
                    ++nr_free;
                    sum_free += yG;
                }
            }

            rho = nr_free > 0 ? sum_free/nr_free : (ub + lb)*0.5;
            r = 0;
        }

        bool select_working_set_nu_svm( int& out_i, int& out_j )
        {
            // return i,j which maximize -grad(f)^T d , under constraint
            // if alpha_i == C, d != +1
            // if alpha_i == 0, d != -1
            double Gmax1 = -DBL_MAX;    // max { -grad(f)_i * d | y_i = +1, d = +1 }
            int Gmax1_idx = -1;

            double Gmax2 = -DBL_MAX;    // max { -grad(f)_i * d | y_i = +1, d = -1 }
            int Gmax2_idx = -1;

            double Gmax3 = -DBL_MAX;    // max { -grad(f)_i * d | y_i = -1, d = +1 }
            int Gmax3_idx = -1;

            double Gmax4 = -DBL_MAX;    // max { -grad(f)_i * d | y_i = -1, d = -1 }
            int Gmax4_idx = -1;

            const schar* y = &y_vec[0];
            const schar* alpha_status = &alpha_status_vec[0];
            const double* G = &G_vec[0];

            for( int i = 0; i < alpha_count; i++ )
            {
                double t;

                if( y[i] > 0 )    // y == +1
                {
                    if( !is_upper_bound(i) && (t = -G[i]) > Gmax1 )  // d = +1
                    {
                        Gmax1 = t;
                        Gmax1_idx = i;
                    }
                    if( !is_lower_bound(i) && (t = G[i]) > Gmax2 )  // d = -1
                    {
                        Gmax2 = t;
                        Gmax2_idx = i;
                    }
                }
                else        // y == -1
                {
                    if( !is_upper_bound(i) && (t = -G[i]) > Gmax3 )  // d = +1
                    {
                        Gmax3 = t;
                        Gmax3_idx = i;
                    }
                    if( !is_lower_bound(i) && (t = G[i]) > Gmax4 )  // d = -1
                    {
                        Gmax4 = t;
                        Gmax4_idx = i;
                    }
                }
            }

            if( MAX(Gmax1 + Gmax2, Gmax3 + Gmax4) < eps )
                return 1;

            if( Gmax1 + Gmax2 > Gmax3 + Gmax4 )
            {
                out_i = Gmax1_idx;
                out_j = Gmax2_idx;
            }
            else
            {
                out_i = Gmax3_idx;
                out_j = Gmax4_idx;
            }
            return 0;
        }

        void calc_rho_nu_svm( double& rho, double& r )
        {
            int nr_free1 = 0, nr_free2 = 0;
            double ub1 = DBL_MAX, ub2 = DBL_MAX;
            double lb1 = -DBL_MAX, lb2 = -DBL_MAX;
            double sum_free1 = 0, sum_free2 = 0;

            const schar* y = &y_vec[0];
            const schar* alpha_status = &alpha_status_vec[0];
            const double* G = &G_vec[0];

            for( int i = 0; i < alpha_count; i++ )
            {
                double G_i = G[i];
                if( y[i] > 0 )
                {
                    if( is_lower_bound(i) )
                        ub1 = MIN( ub1, G_i );
                    else if( is_upper_bound(i) )
                        lb1 = MAX( lb1, G_i );
                    else
                    {
                        ++nr_free1;
                        sum_free1 += G_i;
                    }
                }
                else
                {
                    if( is_lower_bound(i) )
                        ub2 = MIN( ub2, G_i );
                    else if( is_upper_bound(i) )
                        lb2 = MAX( lb2, G_i );
                    else
                    {
                        ++nr_free2;
                        sum_free2 += G_i;
                    }
                }
            }

            double r1 = nr_free1 > 0 ? sum_free1/nr_free1 : (ub1 + lb1)*0.5;
            double r2 = nr_free2 > 0 ? sum_free2/nr_free2 : (ub2 + lb2)*0.5;

            rho = (r1 - r2)*0.5;
            r = (r1 + r2)*0.5;
        }

        /*
        ///////////////////////// construct and solve various formulations ///////////////////////
        */
        static bool solve_c_svc( const Mat& _samples, const vector<schar>& _y,
                                 double _Cp, double _Cn, const Ptr<SVM::Kernel>& _kernel,
                                 vector<double>& _alpha, SolutionInfo& _si, TermCriteria termCrit )
        {
            int sample_count = _samples.rows;

            _alpha.assign(sample_count, 0.);
            vector<double> _b(sample_count, -1.);

            Solver solver( _samples, _y, _alpha, _b, _Cp, _Cn, _kernel,
                           &Solver::get_row_svc,
                           &Solver::select_working_set,
                           &Solver::calc_rho,
                           termCrit );

            if( !solver.solve_generic( _si ))
                return false;

            for( int i = 0; i < sample_count; i++ )
                _alpha[i] *= _y[i];

            return true;
        }


        static bool solve_nu_svc( const Mat& _samples, const vector<schar>& _y,
                                  double nu, const Ptr<SVM::Kernel>& _kernel,
                                  vector<double>& _alpha, SolutionInfo& _si,
                                  TermCriteria termCrit )
        {
            int sample_count = _samples.rows;

            _alpha.resize(sample_count);
            vector<double> _b(sample_count, 0.);

            double sum_pos = nu * sample_count * 0.5;
            double sum_neg = nu * sample_count * 0.5;

            for( int i = 0; i < sample_count; i++ )
            {
                double a;
                if( _y[i] > 0 )
                {
                    a = std::min(1.0, sum_pos);
                    sum_pos -= a;
                }
                else
                {
                    a = std::min(1.0, sum_neg);
                    sum_neg -= a;
                }
                _alpha[i] = a;
            }

            Solver solver( _samples, _y, _alpha, _b, 1., 1., _kernel,
                           &Solver::get_row_svc,
                           &Solver::select_working_set_nu_svm,
                           &Solver::calc_rho_nu_svm,
                           termCrit );

            if( !solver.solve_generic( _si ))
                return false;

            double inv_r = 1./_si.r;

            for( int i = 0; i < sample_count; i++ )
                _alpha[i] *= _y[i]*inv_r;

            _si.rho *= inv_r;
            _si.obj *= (inv_r*inv_r);
            _si.upper_bound_p = inv_r;
            _si.upper_bound_n = inv_r;

            return true;
        }

        static bool solve_one_class( const Mat& _samples, double nu,
                                     const Ptr<SVM::Kernel>& _kernel,
                                     vector<double>& _alpha, SolutionInfo& _si,
                                     TermCriteria termCrit )
        {
            int sample_count = _samples.rows;
            vector<schar> _y(sample_count, 1);
            vector<double> _b(sample_count, 0.);

            int i, n = cvRound( nu*sample_count );

            _alpha.resize(sample_count);
            for( i = 0; i < sample_count; i++ )
                _alpha[i] = i < n ? 1 : 0;

            if( n < sample_count )
                _alpha[n] = nu * sample_count - n;
            else
                _alpha[n-1] = nu * sample_count - (n-1);

            Solver solver( _samples, _y, _alpha, _b, 1., 1., _kernel,
                           &Solver::get_row_one_class,
                           &Solver::select_working_set,
                           &Solver::calc_rho,
                           termCrit );

            return solver.solve_generic(_si);
        }

        static bool solve_eps_svr( const Mat& _samples, const vector<float>& _yf,
                                   double p, double C, const Ptr<SVM::Kernel>& _kernel,
                                   vector<double>& _alpha, SolutionInfo& _si,
                                   TermCriteria termCrit )
        {
            int sample_count = _samples.rows;
            int alpha_count = sample_count*2;

            CV_Assert( (int)_yf.size() == sample_count );

            _alpha.assign(alpha_count, 0.);
            vector<schar> _y(alpha_count);
            vector<double> _b(alpha_count);

            for( int i = 0; i < sample_count; i++ )
            {
                _b[i] = p - _yf[i];
                _y[i] = 1;

                _b[i+sample_count] = p + _yf[i];
                _y[i+sample_count] = -1;
            }

            Solver solver( _samples, _y, _alpha, _b, C, C, _kernel,
                           &Solver::get_row_svr,
                           &Solver::select_working_set,
                           &Solver::calc_rho,
                           termCrit );

            if( !solver.solve_generic( _si ))
                return false;

            for( int i = 0; i < sample_count; i++ )
                _alpha[i] -= _alpha[i+sample_count];

            return true;
        }


        static bool solve_nu_svr( const Mat& _samples, const vector<float>& _yf,
                                  double nu, double C, const Ptr<SVM::Kernel>& _kernel,
                                  vector<double>& _alpha, SolutionInfo& _si,
                                  TermCriteria termCrit )
        {
            int sample_count = _samples.rows;
            int alpha_count = sample_count*2;
            double sum = C * nu * sample_count * 0.5;

            CV_Assert( (int)_yf.size() == sample_count );

            _alpha.resize(alpha_count);
            vector<schar> _y(alpha_count);
            vector<double> _b(alpha_count);

            for( int i = 0; i < sample_count; i++ )
            {
                _alpha[i] = _alpha[i + sample_count] = std::min(sum, C);
                sum -= _alpha[i];

                _b[i] = -_yf[i];
                _y[i] = 1;

                _b[i + sample_count] = _yf[i];
                _y[i + sample_count] = -1;
            }

            Solver solver( _samples, _y, _alpha, _b, 1., 1., _kernel,
                           &Solver::get_row_svr,
                           &Solver::select_working_set_nu_svm,
                           &Solver::calc_rho_nu_svm,
                           termCrit );

            if( !solver.solve_generic( _si ))
                return false;

            for( int i = 0; i < sample_count; i++ )
                _alpha[i] -= _alpha[i+sample_count];

            return true;
        }

        int sample_count;
        int var_count;
        int cache_size;
        int max_cache_size;
        Mat samples;
        SVM::Params params;
        vector<KernelRow> lru_cache;
        int lru_first;
        int lru_last;
        Mat lru_cache_data;

        int alpha_count;

        vector<double> G_vec;
        vector<double>* alpha_vec;
        vector<schar> y_vec;
        // -1 - lower bound, 0 - free, 1 - upper bound
        vector<schar> alpha_status_vec;
        vector<double> b_vec;

        vector<Qfloat> buf[2];
        double eps;
        int max_iter;
        double C[2];  // C[0] == Cn, C[1] == Cp
        Ptr<SVM::Kernel> kernel;

        SelectWorkingSet select_working_set_func;
        CalcRho calc_rho_func;
        GetRow get_row_func;
    };

    //////////////////////////////////////////////////////////////////////////////////////////
    SVMImpl()
    {
        clear();
    }

    ~SVMImpl()
    {
        clear();
    }

    void clear()
    {
        decision_func.clear();
        df_alpha.clear();
        df_index.clear();
        sv.release();
    }

    Mat getSupportVectors() const
    {
        return sv;
    }

    void setParams( const Params& _params, const Ptr<Kernel>& _kernel )
    {
        params = _params;

        int kernelType = params.kernelType;
        int svmType = params.svmType;

        if( kernelType != LINEAR && kernelType != POLY &&
            kernelType != SIGMOID && kernelType != RBF &&
            kernelType != INTER && kernelType != CHI2)
            CV_Error( CV_StsBadArg, "Unknown/unsupported kernel type" );

        if( kernelType == LINEAR )
            params.gamma = 1;
        else if( params.gamma <= 0 )
            CV_Error( CV_StsOutOfRange, "gamma parameter of the kernel must be positive" );

        if( kernelType != SIGMOID && kernelType != POLY )
            params.coef0 = 0;
        else if( params.coef0 < 0 )
            CV_Error( CV_StsOutOfRange, "The kernel parameter <coef0> must be positive or zero" );

        if( kernelType != POLY )
            params.degree = 0;
        else if( params.degree <= 0 )
            CV_Error( CV_StsOutOfRange, "The kernel parameter <degree> must be positive" );

        if( svmType != C_SVC && svmType != NU_SVC &&
            svmType != ONE_CLASS && svmType != EPS_SVR &&
            svmType != NU_SVR )
            CV_Error( CV_StsBadArg, "Unknown/unsupported SVM type" );

        if( svmType == ONE_CLASS || svmType == NU_SVC )
            params.C = 0;
        else if( params.C <= 0 )
            CV_Error( CV_StsOutOfRange, "The parameter C must be positive" );

        if( svmType == C_SVC || svmType == EPS_SVR )
            params.nu = 0;
        else if( params.nu <= 0 || params.nu >= 1 )
            CV_Error( CV_StsOutOfRange, "The parameter nu must be between 0 and 1" );

        if( svmType != EPS_SVR )
            params.p = 0;
        else if( params.p <= 0 )
            CV_Error( CV_StsOutOfRange, "The parameter p must be positive" );

        if( svmType != C_SVC )
            params.classWeights.release();

        termCrit = params.termCrit;
        if( !(termCrit.type & TermCriteria::EPS) )
            termCrit.epsilon = DBL_EPSILON;
        termCrit.epsilon = std::max(termCrit.epsilon, DBL_EPSILON);
        if( !(termCrit.type & TermCriteria::COUNT) )
            termCrit.maxCount = INT_MAX;
        termCrit.maxCount = std::max(termCrit.maxCount, 1);

        if( _kernel )
            kernel = _kernel;
        else
            kernel = makePtr<SVMKernelImpl>(params);
    }

    Params getParams() const
    {
        return params;
    }

    Ptr<Kernel> getKernel() const
    {
        return kernel;
    }

    int getSVCount(int i) const
    {
        return (i < (int)(decision_func.size()-1) ? decision_func[i+1].ofs :
                (int)df_index.size()) - decision_func[i].ofs;
    }

    bool do_train( const Mat& _samples, const Mat& _responses )
    {
        int svmType = params.svmType;
        int i, j, k, sample_count = _samples.rows;
        vector<double> _alpha;
        Solver::SolutionInfo sinfo;

        CV_Assert( _samples.type() == CV_32F );
        var_count = _samples.cols;

        if( svmType == ONE_CLASS || svmType == EPS_SVR || svmType == NU_SVR )
        {
            int sv_count = 0;
            decision_func.clear();

            vector<float> _yf;
            if( !_responses.empty() )
                _responses.convertTo(_yf, CV_32F);

            bool ok =
            svmType == ONE_CLASS ? Solver::solve_one_class( _samples, params.nu, kernel, _alpha, sinfo, termCrit ) :
            svmType == EPS_SVR ? Solver::solve_eps_svr( _samples, _yf, params.p, params.C, kernel, _alpha, sinfo, termCrit ) :
            svmType == NU_SVR ? Solver::solve_nu_svr( _samples, _yf, params.nu, params.C, kernel, _alpha, sinfo, termCrit ) : false;

            if( !ok )
                return false;

            for( i = 0; i < sample_count; i++ )
                sv_count += fabs(_alpha[i]) > 0;

            CV_Assert(sv_count != 0);

            sv.create(sv_count, _samples.cols, CV_32F);
            df_alpha.resize(sv_count);
            df_index.resize(sv_count);

            for( i = k = 0; i < sample_count; i++ )
            {
                if( std::abs(_alpha[i]) > 0 )
                {
                    _samples.row(i).copyTo(sv.row(k));
                    df_alpha[k] = _alpha[i];
                    df_index[k] = k;
                    k++;
                }
            }

            decision_func.push_back(DecisionFunc(sinfo.rho, 0));
        }
        else
        {
            int class_count = (int)class_labels.total();
            vector<int> svidx, sidx, sidx_all, sv_tab(sample_count, 0);
            Mat temp_samples, class_weights;
            vector<int> class_ranges;
            vector<schar> temp_y;
            double nu = params.nu;
            CV_Assert( svmType == C_SVC || svmType == NU_SVC );

            if( svmType == C_SVC && !params.classWeights.empty() )
            {
                const Mat cw = params.classWeights;

                if( (cw.cols != 1 && cw.rows != 1) ||
                    (int)cw.total() != class_count ||
                    (cw.type() != CV_32F && cw.type() != CV_64F) )
                    CV_Error( CV_StsBadArg, "params.class_weights must be 1d floating-point vector "
                        "containing as many elements as the number of classes" );

                cw.convertTo(class_weights, CV_64F, params.C);
                //normalize(cw, class_weights, params.C, 0, NORM_L1, CV_64F);
            }

            decision_func.clear();
            df_alpha.clear();
            df_index.clear();

            sortSamplesByClasses( _samples, _responses, sidx_all, class_ranges );

            //check that while cross-validation there were the samples from all the classes
            if( class_ranges[class_count] <= 0 )
                CV_Error( CV_StsBadArg, "While cross-validation one or more of the classes have "
                "been fell out of the sample. Try to enlarge <CvSVMParams::k_fold>" );

            if( svmType == NU_SVC )
            {
                // check if nu is feasible
                for( i = 0; i < class_count; i++ )
                {
                    int ci = class_ranges[i+1] - class_ranges[i];
                    for( j = i+1; j< class_count; j++ )
                    {
                        int cj = class_ranges[j+1] - class_ranges[j];
                        if( nu*(ci + cj)*0.5 > std::min( ci, cj ) )
                            // TODO: add some diagnostic
                            return false;
                    }
                }
            }

            size_t samplesize = _samples.cols*_samples.elemSize();

            // train n*(n-1)/2 classifiers
            for( i = 0; i < class_count; i++ )
            {
                for( j = i+1; j < class_count; j++ )
                {
                    int si = class_ranges[i], ci = class_ranges[i+1] - si;
                    int sj = class_ranges[j], cj = class_ranges[j+1] - sj;
                    double Cp = params.C, Cn = Cp;

                    temp_samples.create(ci + cj, _samples.cols, _samples.type());
                    sidx.resize(ci + cj);
                    temp_y.resize(ci + cj);

                    // form input for the binary classification problem
                    for( k = 0; k < ci+cj; k++ )
                    {
                        int idx = k < ci ? si+k : sj+k-ci;
                        memcpy(temp_samples.ptr(k), _samples.ptr(sidx_all[idx]), samplesize);
                        sidx[k] = sidx_all[idx];
                        temp_y[k] = k < ci ? 1 : -1;
                    }

                    if( !class_weights.empty() )
                    {
                        Cp = class_weights.at<double>(i);
                        Cn = class_weights.at<double>(j);
                    }

                    DecisionFunc df;
                    bool ok = params.svmType == C_SVC ?
                                Solver::solve_c_svc( temp_samples, temp_y, Cp, Cn,
                                                     kernel, _alpha, sinfo, termCrit ) :
                              params.svmType == NU_SVC ?
                                Solver::solve_nu_svc( temp_samples, temp_y, params.nu,
                                                      kernel, _alpha, sinfo, termCrit ) :
                              false;
                    if( !ok )
                        return false;
                    df.rho = sinfo.rho;
                    df.ofs = (int)df_index.size();
                    decision_func.push_back(df);

                    for( k = 0; k < ci + cj; k++ )
                    {
                        if( std::abs(_alpha[k]) > 0 )
                        {
                            int idx = k < ci ? si+k : sj+k-ci;
                            sv_tab[sidx_all[idx]] = 1;
                            df_index.push_back(sidx_all[idx]);
                            df_alpha.push_back(_alpha[k]);
                        }
                    }
                }
            }

            // allocate support vectors and initialize sv_tab
            for( i = 0, k = 0; i < sample_count; i++ )
            {
                if( sv_tab[i] )
                    sv_tab[i] = ++k;
            }

            int sv_total = k;
            sv.create(sv_total, _samples.cols, _samples.type());

            for( i = 0; i < sample_count; i++ )
            {
                if( !sv_tab[i] )
                    continue;
                memcpy(sv.ptr(sv_tab[i]-1), _samples.ptr(i), samplesize);
            }

            // set sv pointers
            int n = (int)df_index.size();
            for( i = 0; i < n; i++ )
            {
                CV_Assert( sv_tab[df_index[i]] > 0 );
                df_index[i] = sv_tab[df_index[i]] - 1;
            }
        }

        optimize_linear_svm();
        return true;
    }

    void optimize_linear_svm()
    {
        // we optimize only linear SVM: compress all the support vectors into one.
        if( params.kernelType != LINEAR )
            return;

        int i, df_count = (int)decision_func.size();

        for( i = 0; i < df_count; i++ )
        {
            if( getSVCount(i) != 1 )
                break;
        }

        // if every decision functions uses a single support vector;
        // it's already compressed. skip it then.
        if( i == df_count )
            return;

        AutoBuffer<double> vbuf(var_count);
        double* v = vbuf;
        Mat new_sv(df_count, var_count, CV_32F);

        vector<DecisionFunc> new_df;

        for( i = 0; i < df_count; i++ )
        {
            float* dst = new_sv.ptr<float>(i);
            memset(v, 0, var_count*sizeof(v[0]));
            int j, k, sv_count = getSVCount(i);
            const DecisionFunc& df = decision_func[i];
            const int* sv_index = &df_index[df.ofs];
            const double* sv_alpha = &df_alpha[df.ofs];
            for( j = 0; j < sv_count; j++ )
            {
                const float* src = sv.ptr<float>(sv_index[j]);
                double a = sv_alpha[j];
                for( k = 0; k < var_count; k++ )
                    v[k] += src[k]*a;
            }
            for( k = 0; k < var_count; k++ )
                dst[k] = (float)v[k];
            new_df.push_back(DecisionFunc(df.rho, i));
        }

        setRangeVector(df_index, df_count);
        df_alpha.assign(df_count, 1.);
        std::swap(sv, new_sv);
        std::swap(decision_func, new_df);
    }

    bool train( const Ptr<TrainData>& data, int )
    {
        clear();

        int svmType = params.svmType;
        Mat samples = data->getTrainSamples();
        Mat responses;

        if( svmType == C_SVC || svmType == NU_SVC )
        {
            responses = data->getTrainNormCatResponses();
            if( responses.empty() )
                CV_Error(CV_StsBadArg, "in the case of classification problem the responses must be categorical; "
                                       "either specify varType when creating TrainData, or pass integer responses");
            class_labels = data->getClassLabels();
        }
        else
            responses = data->getTrainResponses();

        if( !do_train( samples, responses ))
        {
            clear();
            return false;
        }

        return true;
    }

    bool trainAuto( const Ptr<TrainData>& data, int k_fold,
                    ParamGrid C_grid, ParamGrid gamma_grid, ParamGrid p_grid,
                    ParamGrid nu_grid, ParamGrid coef_grid, ParamGrid degree_grid,
                    bool balanced )
    {
        int svmType = params.svmType;
        RNG rng((uint64)-1);

        if( svmType == ONE_CLASS )
            // current implementation of "auto" svm does not support the 1-class case.
            return train( data, 0 );

        clear();

        CV_Assert( k_fold >= 2 );

        // All the parameters except, possibly, <coef0> are positive.
        // <coef0> is nonnegative
        #define CHECK_GRID(grid, param) \
        if( grid.logStep <= 1 ) \
        { \
            grid.minVal = grid.maxVal = params.param; \
            grid.logStep = 10; \
        } \
        else \
            checkParamGrid(grid)

        CHECK_GRID(C_grid, C);
        CHECK_GRID(gamma_grid, gamma);
        CHECK_GRID(p_grid, p);
        CHECK_GRID(nu_grid, nu);
        CHECK_GRID(coef_grid, coef0);
        CHECK_GRID(degree_grid, degree);

        // these parameters are not used:
        if( params.kernelType != POLY )
            degree_grid.minVal = degree_grid.maxVal = params.degree;
        if( params.kernelType == LINEAR )
            gamma_grid.minVal = gamma_grid.maxVal = params.gamma;
        if( params.kernelType != POLY && params.kernelType != SIGMOID )
            coef_grid.minVal = coef_grid.maxVal = params.coef0;
        if( svmType == NU_SVC || svmType == ONE_CLASS )
            C_grid.minVal = C_grid.maxVal = params.C;
        if( svmType == C_SVC || svmType == EPS_SVR )
            nu_grid.minVal = nu_grid.maxVal = params.nu;
        if( svmType != EPS_SVR )
            p_grid.minVal = p_grid.maxVal = params.p;

        Mat samples = data->getTrainSamples();
        Mat responses;
        bool is_classification = false;
        Mat class_labels0 = class_labels;
        int class_count = (int)class_labels.total();

        if( svmType == C_SVC || svmType == NU_SVC )
        {
            responses = data->getTrainNormCatResponses();
            class_labels = data->getClassLabels();
            is_classification = true;

            vector<int> temp_class_labels;
            setRangeVector(temp_class_labels, class_count);

            // temporarily replace class labels with 0, 1, ..., NCLASSES-1
            Mat(temp_class_labels).copyTo(class_labels);
        }
        else
            responses = data->getTrainResponses();

        CV_Assert(samples.type() == CV_32F);

        int sample_count = samples.rows;
        var_count = samples.cols;
        size_t sample_size = var_count*samples.elemSize();

        vector<int> sidx;
        setRangeVector(sidx, sample_count);

        int i, j, k;

        // randomly permute training samples
        for( i = 0; i < sample_count; i++ )
        {
            int i1 = rng.uniform(0, sample_count);
            int i2 = rng.uniform(0, sample_count);
            std::swap(sidx[i1], sidx[i2]);
        }

        if( is_classification && class_count == 2 && balanced )
        {
            // reshuffle the training set in such a way that
            // instances of each class are divided more or less evenly
            // between the k_fold parts.
            vector<int> sidx0, sidx1;

            for( i = 0; i < sample_count; i++ )
            {
                if( responses.at<int>(sidx[i]) == 0 )
                    sidx0.push_back(sidx[i]);
                else
                    sidx1.push_back(sidx[i]);
            }

            int n0 = (int)sidx0.size(), n1 = (int)sidx1.size();
            int a0 = 0, a1 = 0;
            sidx.clear();
            for( k = 0; k < k_fold; k++ )
            {
                int b0 = ((k+1)*n0 + k_fold/2)/k_fold, b1 = ((k+1)*n1 + k_fold/2)/k_fold;
                int a = (int)sidx.size(), b = a + (b0 - a0) + (b1 - a1);
                for( i = a0; i < b0; i++ )
                    sidx.push_back(sidx0[i]);
                for( i = a1; i < b1; i++ )
                    sidx.push_back(sidx1[i]);
                for( i = 0; i < (b - a); i++ )
                {
                    int i1 = rng.uniform(a, b);
                    int i2 = rng.uniform(a, b);
                    std::swap(sidx[i1], sidx[i2]);
                }
                a0 = b0; a1 = b1;
            }
        }

        int test_sample_count = (sample_count + k_fold/2)/k_fold;
        int train_sample_count = sample_count - test_sample_count;

        Params best_params = params;
        double min_error = FLT_MAX;

        int rtype = responses.type();

        Mat temp_train_samples(train_sample_count, var_count, CV_32F);
        Mat temp_test_samples(test_sample_count, var_count, CV_32F);
        Mat temp_train_responses(train_sample_count, 1, rtype);
        Mat temp_test_responses;

        #define FOR_IN_GRID(var, grid) \
            for( params.var = grid.minVal; params.var == grid.minVal || params.var < grid.maxVal; params.var *= grid.logStep )

        FOR_IN_GRID(C, C_grid)
        FOR_IN_GRID(gamma, gamma_grid)
        FOR_IN_GRID(p, p_grid)
        FOR_IN_GRID(nu, nu_grid)
        FOR_IN_GRID(coef0, coef_grid)
        FOR_IN_GRID(degree, degree_grid)
        {
            // make sure we updated the kernel and other parameters
            setParams(params, Ptr<Kernel>() );

            double error = 0;
            for( k = 0; k < k_fold; k++ )
            {
                int start = (k*sample_count + k_fold/2)/k_fold;
                for( i = 0; i < train_sample_count; i++ )
                {
                    j = sidx[(i+start)%sample_count];
                    memcpy(temp_train_samples.ptr(i), samples.ptr(j), sample_size);
                    if( is_classification )
                        temp_train_responses.at<int>(i) = responses.at<int>(j);
                    else if( !responses.empty() )
                        temp_train_responses.at<float>(i) = responses.at<float>(j);
                }

                // Train SVM on <train_size> samples
                if( !do_train( temp_train_samples, temp_train_responses ))
                    continue;

                for( i = 0; i < test_sample_count; i++ )
                {
                    j = sidx[(i+start+train_sample_count) % sample_count];
                    memcpy(temp_train_samples.ptr(i), samples.ptr(j), sample_size);
                }

                predict(temp_test_samples, temp_test_responses, 0);
                for( i = 0; i < test_sample_count; i++ )
                {
                    float val = temp_test_responses.at<float>(i);
                    j = sidx[(i+start+train_sample_count) % sample_count];
                    if( is_classification )
                        error += (float)(val != responses.at<int>(j));
                    else
                    {
                        val -= responses.at<float>(j);
                        error += val*val;
                    }
                }
            }
            if( min_error > error )
            {
                min_error   = error;
                best_params = params;
            }
        }

        params = best_params;
        class_labels = class_labels0;
        return do_train( samples, responses );
    }

    struct PredictBody : ParallelLoopBody
    {
        PredictBody( const SVMImpl* _svm, const Mat& _samples, Mat& _results, bool _returnDFVal )
        {
            svm = _svm;
            results = &_results;
            samples = &_samples;
            returnDFVal = _returnDFVal;
        }

        void operator()( const Range& range ) const
        {
            int svmType = svm->params.svmType;
            int sv_total = svm->sv.rows;
            int class_count = !svm->class_labels.empty() ? (int)svm->class_labels.total() : svmType == ONE_CLASS ? 1 : 0;

            AutoBuffer<float> _buffer(sv_total + (class_count+1)*2);
            float* buffer = _buffer;

            int i, j, dfi, k, si;

            if( svmType == EPS_SVR || svmType == NU_SVR || svmType == ONE_CLASS )
            {
                for( si = range.start; si < range.end; si++ )
                {
                    const float* row_sample = samples->ptr<float>(si);
                    svm->kernel->calc( sv_total, svm->var_count, svm->sv.ptr<float>(), row_sample, buffer );

                    const SVMImpl::DecisionFunc* df = &svm->decision_func[0];
                    double sum = -df->rho;
                    for( i = 0; i < sv_total; i++ )
                        sum += buffer[i]*svm->df_alpha[i];
                    float result = svm->params.svmType == ONE_CLASS && !returnDFVal ? (float)(sum > 0) : (float)sum;
                    results->at<float>(si) = result;
                }
            }
            else if( svmType == C_SVC || svmType == NU_SVC )
            {
                int* vote = (int*)(buffer + sv_total);

                for( si = range.start; si < range.end; si++ )
                {
                    svm->kernel->calc( sv_total, svm->var_count, svm->sv.ptr<float>(),
                                       samples->ptr<float>(si), buffer );
                    double sum = 0.;

                    memset( vote, 0, class_count*sizeof(vote[0]));

                    for( i = dfi = 0; i < class_count; i++ )
                    {
                        for( j = i+1; j < class_count; j++, dfi++ )
                        {
                            const DecisionFunc& df = svm->decision_func[dfi];
                            sum = -df.rho;
                            int sv_count = svm->getSVCount(dfi);
                            const double* alpha = &svm->df_alpha[df.ofs];
                            const int* sv_index = &svm->df_index[df.ofs];
                            for( k = 0; k < sv_count; k++ )
                                sum += alpha[k]*buffer[sv_index[k]];

                            vote[sum > 0 ? i : j]++;
                        }
                    }

                    for( i = 1, k = 0; i < class_count; i++ )
                    {
                        if( vote[i] > vote[k] )
                            k = i;
                    }
                    float result = returnDFVal && class_count == 2 ?
                        (float)sum : (float)(svm->class_labels.at<int>(k));
                    results->at<float>(si) = result;
                }
            }
            else
                CV_Error( CV_StsBadArg, "INTERNAL ERROR: Unknown SVM type, "
                         "the SVM structure is probably corrupted" );
        }

        const SVMImpl* svm;
        const Mat* samples;
        Mat* results;
        bool returnDFVal;
    };

    float predict( InputArray _samples, OutputArray _results, int flags ) const
    {
        float result = 0;
        Mat samples = _samples.getMat(), results;
        int nsamples = samples.rows;
        bool returnDFVal = (flags & RAW_OUTPUT) != 0;

        CV_Assert( samples.cols == var_count && samples.type() == CV_32F );

        if( _results.needed() )
        {
            _results.create( nsamples, 1, samples.type() );
            results = _results.getMat();
        }
        else
        {
            CV_Assert( nsamples == 1 );
            results = Mat(1, 1, CV_32F, &result);
        }

        PredictBody invoker(this, samples, results, returnDFVal);
        if( nsamples < 10 )
            invoker(Range(0, nsamples));
        else
            parallel_for_(Range(0, nsamples), invoker);
        return result;
    }

    double getDecisionFunction(int i, OutputArray _alpha, OutputArray _svidx ) const
    {
        CV_Assert( 0 <= i && i < (int)decision_func.size());
        const DecisionFunc& df = decision_func[i];
        int count = getSVCount(i);
        Mat(1, count, CV_64F, (double*)&df_alpha[df.ofs]).copyTo(_alpha);
        Mat(1, count, CV_32S, (int*)&df_index[df.ofs]).copyTo(_svidx);
        return df.rho;
    }

    void write_params( FileStorage& fs ) const
    {
        int svmType = params.svmType;
        int kernelType = params.kernelType;

        String svm_type_str =
            svmType == C_SVC ? "C_SVC" :
            svmType == NU_SVC ? "NU_SVC" :
            svmType == ONE_CLASS ? "ONE_CLASS" :
            svmType == EPS_SVR ? "EPS_SVR" :
            svmType == NU_SVR ? "NU_SVR" : format("Uknown_%d", svmType);
        String kernel_type_str =
            kernelType == LINEAR ? "LINEAR" :
            kernelType == POLY ? "POLY" :
            kernelType == RBF ? "RBF" :
            kernelType == SIGMOID ? "SIGMOID" : format("Unknown_%d", kernelType);

        fs << "svmType" << svm_type_str;

        // save kernel
        fs << "kernel" << "{" << "type" << kernel_type_str;

        if( kernelType == POLY )
            fs << "degree" << params.degree;

        if( kernelType != LINEAR )
            fs << "gamma" << params.gamma;

        if( kernelType == POLY || kernelType == SIGMOID )
            fs << "coef0" << params.coef0;

        fs << "}";

        if( svmType == C_SVC || svmType == EPS_SVR || svmType == NU_SVR )
            fs << "C" << params.C;

        if( svmType == NU_SVC || svmType == ONE_CLASS || svmType == NU_SVR )
            fs << "nu" << params.nu;

        if( svmType == EPS_SVR )
            fs << "p" << params.p;

        fs << "term_criteria" << "{:";
        if( params.termCrit.type & TermCriteria::EPS )
            fs << "epsilon" << params.termCrit.epsilon;
        if( params.termCrit.type & TermCriteria::COUNT )
            fs << "iterations" << params.termCrit.maxCount;
        fs << "}";
    }

    bool isTrained() const
    {
        return !sv.empty();
    }

    bool isClassifier() const
    {
        return params.svmType == C_SVC || params.svmType == NU_SVC || params.svmType == ONE_CLASS;
    }

    int getVarCount() const
    {
        return var_count;
    }

    String getDefaultModelName() const
    {
        return "opencv_ml_svm";
    }

    void write( FileStorage& fs ) const
    {
        int class_count = !class_labels.empty() ? (int)class_labels.total() :
                          params.svmType == ONE_CLASS ? 1 : 0;
        if( !isTrained() )
            CV_Error( CV_StsParseError, "SVM model data is invalid, check sv_count, var_* and class_count tags" );

        write_params( fs );

        fs << "var_count" << var_count;

        if( class_count > 0 )
        {
            fs << "class_count" << class_count;

            if( !class_labels.empty() )
                fs << "class_labels" << class_labels;

            if( !params.classWeights.empty() )
                fs << "class_weights" << params.classWeights;
        }

        // write the joint collection of support vectors
        int i, sv_total = sv.rows;
        fs << "sv_total" << sv_total;
        fs << "support_vectors" << "[";
        for( i = 0; i < sv_total; i++ )
        {
            fs << "[:";
            fs.writeRaw("f", sv.ptr(i), sv.cols*sv.elemSize());
            fs << "]";
        }
        fs << "]";

        // write decision functions
        int df_count = (int)decision_func.size();

        fs << "decision_functions" << "[";
        for( i = 0; i < df_count; i++ )
        {
            const DecisionFunc& df = decision_func[i];
            int sv_count = getSVCount(i);
            fs << "{" << "sv_count" << sv_count
               << "rho" << df.rho
               << "alpha" << "[:";
            fs.writeRaw("d", (const uchar*)&df_alpha[df.ofs], sv_count*sizeof(df_alpha[0]));
            fs << "]";
            if( class_count > 2 )
            {
                fs << "index" << "[:";
                fs.writeRaw("i", (const uchar*)&df_index[df.ofs], sv_count*sizeof(df_index[0]));
                fs << "]";
            }
            else
                CV_Assert( sv_count == sv_total );
            fs << "}";
        }
        fs << "]";
    }

    void read_params( const FileNode& fn )
    {
        Params _params;

        String svm_type_str = (String)fn["svmType"];
        int svmType =
            svm_type_str == "C_SVC" ? C_SVC :
            svm_type_str == "NU_SVC" ? NU_SVC :
            svm_type_str == "ONE_CLASS" ? ONE_CLASS :
            svm_type_str == "EPS_SVR" ? EPS_SVR :
            svm_type_str == "NU_SVR" ? NU_SVR : -1;

        if( svmType < 0 )
            CV_Error( CV_StsParseError, "Missing of invalid SVM type" );

        FileNode kernel_node = fn["kernel"];
        if( kernel_node.empty() )
            CV_Error( CV_StsParseError, "SVM kernel tag is not found" );

        String kernel_type_str = (String)kernel_node["type"];
        int kernelType =
            kernel_type_str == "LINEAR" ? LINEAR :
            kernel_type_str == "POLY" ? POLY :
            kernel_type_str == "RBF" ? RBF :
            kernel_type_str == "SIGMOID" ? SIGMOID : -1;

        if( kernelType < 0 )
            CV_Error( CV_StsParseError, "Missing of invalid SVM kernel type" );

        _params.svmType = svmType;
        _params.kernelType = kernelType;
        _params.degree = (double)kernel_node["degree"];
        _params.gamma = (double)kernel_node["gamma"];
        _params.coef0 = (double)kernel_node["coef0"];

        _params.C = (double)fn["C"];
        _params.nu = (double)fn["nu"];
        _params.p = (double)fn["p"];
        _params.classWeights = Mat();

        FileNode tcnode = fn["term_criteria"];
        if( !tcnode.empty() )
        {
            _params.termCrit.epsilon = (double)tcnode["epsilon"];
            _params.termCrit.maxCount = (int)tcnode["iterations"];
            _params.termCrit.type = (_params.termCrit.epsilon > 0 ? TermCriteria::EPS : 0) +
                                   (_params.termCrit.maxCount > 0 ? TermCriteria::COUNT : 0);
        }
        else
            _params.termCrit = TermCriteria( TermCriteria::EPS + TermCriteria::COUNT, 1000, FLT_EPSILON );

        setParams( _params, Ptr<Kernel>() );
    }

    void read( const FileNode& fn )
    {
        clear();

        // read SVM parameters
        read_params( fn );

        // and top-level data
        int i, sv_total = (int)fn["sv_total"];
        var_count = (int)fn["var_count"];
        int class_count = (int)fn["class_count"];

        if( sv_total <= 0 || var_count <= 0 )
            CV_Error( CV_StsParseError, "SVM model data is invalid, check sv_count, var_* and class_count tags" );

        FileNode m = fn["class_labels"];
        if( !m.empty() )
            m >> class_labels;
        m = fn["class_weights"];
        if( !m.empty() )
            m >> params.classWeights;

        if( class_count > 1 && (class_labels.empty() || (int)class_labels.total() != class_count))
            CV_Error( CV_StsParseError, "Array of class labels is missing or invalid" );

        // read support vectors
        FileNode sv_node = fn["support_vectors"];

        CV_Assert((int)sv_node.size() == sv_total);
        sv.create(sv_total, var_count, CV_32F);

        FileNodeIterator sv_it = sv_node.begin();
        for( i = 0; i < sv_total; i++, ++sv_it )
        {
            (*sv_it).readRaw("f", sv.ptr(i), var_count*sv.elemSize());
        }

        // read decision functions
        int df_count = class_count > 1 ? class_count*(class_count-1)/2 : 1;
        FileNode df_node = fn["decision_functions"];

        CV_Assert((int)df_node.size() == df_count);

        FileNodeIterator df_it = df_node.begin();
        for( i = 0; i < df_count; i++, ++df_it )
        {
            FileNode dfi = *df_it;
            DecisionFunc df;
            int sv_count = (int)dfi["sv_count"];
            int ofs = (int)df_index.size();
            df.rho = (double)dfi["rho"];
            df.ofs = ofs;
            df_index.resize(ofs + sv_count);
            df_alpha.resize(ofs + sv_count);
            dfi["alpha"].readRaw("d", (uchar*)&df_alpha[ofs], sv_count*sizeof(df_alpha[0]));
            if( class_count > 2 )
                dfi["index"].readRaw("i", (uchar*)&df_index[ofs], sv_count*sizeof(df_index[0]));
            decision_func.push_back(df);
        }
        if( class_count <= 2 )
            setRangeVector(df_index, sv_total);
        if( (int)fn["optimize_linear"] != 0 )
            optimize_linear_svm();
    }

    Params params;
    TermCriteria termCrit;
    Mat class_labels;
    int var_count;
    Mat sv;
    vector<DecisionFunc> decision_func;
    vector<double> df_alpha;
    vector<int> df_index;

    Ptr<Kernel> kernel;
};


Ptr<SVM> SVM::create(const Params& params, const Ptr<SVM::Kernel>& kernel)
{
    Ptr<SVMImpl> p = makePtr<SVMImpl>();
    p->setParams(params, kernel);




    return p;
}

}
}

/* End of file. */

