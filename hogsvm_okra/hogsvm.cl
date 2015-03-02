__kernel void svmlinear(__global float* vecs,
                  __global float* another,
                  __const unsigned int vcount,
                  __const unsigned int var_count,
                  __const float alpha,
                  __const float beta,
                  __global float* results
                   )
{
    int id ;
    int offset; 
    int k;
    float s = 0;
    
    id = get_global_id(0);
    offset = id*var_count;
/*    
        for( k = 0; k <= var_count - 4; k += 4 )
            s += vecs[ offset + k]*another[k] + vecs[ offset + k+1]*another[k+1] + vecs[ offset + k+2 ]*another[k+2] + vecs[ offset + k+3]*another[k+3];
        for( ; k < var_count; k++ )
            s += vecs[ offset + k ]*another[k];
    
        results[id] = (s*alpha + beta);
	*/
}
