__kernel void matmul_fp32_kernel(__global const float* a,
                                  __global const float* b,
                                  __global float* c,
                                  int n)
{
    const int col = get_global_id(0);
    const int row = get_global_id(1);
    if ((col >= n) || (row >= n))
        return;

    float acc = 0.0f;
    for (int k = 0; k < n; ++k)
        acc += a[row * n + k] * b[k * n + col];
    c[row * n + col] = acc;
}
