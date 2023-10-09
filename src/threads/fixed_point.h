#ifndef fix_POINT_H
#define fix_POINT_H

static const int f = 1 << 14;

static int int_to_fix(int n)
{
    return n * f;
}

static int fix_to_int_roundzero(int x)
{
    return x / f;
}

static int fix_to_int_roundnear(int x)
{
    if (x >= 0) return (x + f / 2) / f;
    return (x - f / 2) / f;
}

static int fix_add_fix(int x, int y)
{
    return x + y;
}

static int fix_sub_fix(int x, int y)
{
    return fix_add_fix(x, -y);
}

static int fix_add_int(int x, int n)
{
    return x + n * f;
}

static int fix_sub_int(int x, int n)
{
    return x - n * f;
}

static int fix_mul_fix(int x, int y)
{
    return ((int64_t) x) * y / f;
}

static int fix_mul_int(int x, int n)
{
    return x * n;
}

static int fix_div_fix(int x, int y)
{
    return ((int64_t) x) * f / y;
}

static int fix_div_int(int x, int n)
{
    return x / n;
}

#endif