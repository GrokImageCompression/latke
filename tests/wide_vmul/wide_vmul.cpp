#include <hls_vector.h>

using vfloat16 = hls::vector<float, 16>;

extern "C"
{
void wide_vmul(
        const vfloat16 *in1,
        const vfloat16 *in2,
		vfloat16 *out,
		int size
    )
    {
#pragma HLS INTERFACE m_axi depth=8192 port=in1 max_read_burst_length=32 offset=slave bundle=gmem
#pragma HLS INTERFACE m_axi depth=8192 port = in2 max_read_burst_length = 32 offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi depth=8192 port = out max_write_burst_length = 32 max_read_burst_length = 32 offset = slave bundle = gmem2
#pragma HLS INTERFACE s_axilite port = in1 bundle = control
#pragma HLS INTERFACE s_axilite port = in2 bundle = control
#pragma HLS INTERFACE s_axilite port = out bundle = control
#pragma HLS INTERFACE s_axilite port = size bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

		for (int i = 0; i < size/16; i++)
		{
			out[i] = in1[i] * in2[i];
		}
    }
}
