#pragma OPENCL EXTENSION cl_khr_fp64 : enable

#define _DEBUGx

__kernel void svmlinear(__global float* vecs,
                  __global float* another,
                  __const unsigned int vcount,
                  __const unsigned int var_count,
                  __const float alpha,
                  __const float beta,
                  __global float* results
                   )
{
    int id = get_global_id(0);
    int offset = id*var_count;
    int k;
    //printf("work-item %d begin\n",id);

    
        double s = 0;
    
        for( k = 0; k <= var_count - 4; k += 4 )
            s += vecs[ offset + k]*another[k] + vecs[ offset + k+1]*another[k+1] + vecs[ offset + k+2 ]*another[k+2] + vecs[ offset + k+3]*another[k+3];
        for( ; k < var_count; k++ )
            s += vecs[ offset + k ]*another[k];
    
        results[id] = (float)(s*alpha + beta);
    
#ifdef _DEBUG
    if(id==vcount-1){
        for(k=0;k<var_count;k++){
            printf("indevice, vecs[%d]=%f\n",k,vecs[offset + k]);
        }
        printf("results=%f\n",id,results[id]);
    }
#endif
    
    
    
    


    

}
