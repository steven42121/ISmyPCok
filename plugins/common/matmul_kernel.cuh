// Generic FP32 matrix multiplication kernel shared by the CUDA and HIP
// backends. C = A * B for N x N matrices with N = 1024.
// Block = 16 x 16; each thread computes one output element.

__global__ void matmul_fp32_kernel(const float* __restrict__ a,
                                   const float* __restrict__ b,
                                   float* __restrict__ c,
                                   int n)
{
    const int col = blockIdx.x * blockDim.x + threadIdx.x;
    const int row = blockIdx.y * blockDim.y + threadIdx.y;
    if ((col >= n) || (row >= n))
        return;

    float acc = 0.0f;
    for (int k = 0; k < n; ++k)
        acc += a[row * n + k] * b[k * n + col];
    c[row * n + col] = acc;
}
