/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#pragma once

// Maclaurin Series for the exponent function
template <typename Type>
struct maclaurin_exp
{
    static_assert(std::is_arithmetic<Type>::value, "Arithmetic type is required");
    using _Type = typename std::conditional<std::is_integral<Type>::value, float, Type>::type;

    std::size_t series_number{100};

    _Type
    operator()(const Type& arg)
    {
        _Type exp_sum = _Type(1);
        _Type i_member = _Type(1);
        for (std::size_t i = 1; i < series_number; ++i)
        {
            i_member *= arg / i;
            exp_sum += i_member;
        }
        return exp_sum;
    }
};

// Maclaurin Series for function f(x,y) = exp(x) * ( sin(y) + cos(y) )
template <typename Type>
struct maclaurin_2d_func1
{
    static_assert(std::is_arithmetic<Type>::value, "Arithmetic type is required");
    using _Type = typename std::conditional<std::is_integral<Type>::value, float, Type>::type;

    std::size_t series_number_1d;
    std::size_t series_number_2d;

    maclaurin_2d_func1() : series_number_1d(50), series_number_2d(50) {};
    
    _Type
    operator()(const _Type& x, const _Type& y)
    {
        _Type series_sum = y + _Type(1); // <i = 0; j = 0>
        _Type i_member = _Type(1);
        _Type j_member = _Type(1);

        // j = 0
        for (std::size_t i = 1; i < series_number_1d; ++i)
        {
            i_member *= x / i;
            series_sum += i_member * (y + 1);
        }

        // i = 0
        for (std::size_t j = 1; j < series_number_2d; ++j)
        {
            j_member *= -(y * y) / (2 * j * (2 * j - 1));
            series_sum += j_member * (y / (2 * j + 1) + 1);
        }
      
        i_member = Type(1);
        for (std::size_t i = 1; i < series_number_1d; ++i)
        {
            i_member *= x / i;
            j_member = _Type(1);
            for (std::size_t j = 1; j < series_number_2d; ++j)
            {
                j_member *= -(y * y) / (2 * j * (2 * j - 1));
                series_sum += i_member * j_member * (y / (2 * j + 1) + 1);
            }
        }
        return series_sum;
    }
};

// Maclaurin Series for function f(x,y) = ln(1 + x) * ln(1 + y); |x| + |y| < 1
template <typename Type>
struct maclaurin_2d_func2
{
    static_assert(std::is_arithmetic<Type>::value, "Arithmetic type is required");
    using _Type = typename std::conditional<std::is_integral<Type>::value, float, Type>::type;

    std::size_t series_number_1d;
    std::size_t series_number_2d;

    maclaurin_2d_func2() : series_number_1d(50), series_number_2d(50) {};
    
    _Type
    operator()(const _Type& x, const _Type& y)
    {
        _Type series_sum = y + _Type(0);
        _Type i_member = _Type(1);
        for (std::size_t i = 1; i < series_number_1d; ++i)
        {
            i_member *= x / i;
            _Type j_member = _Type(1);
            for (std::size_t j = 1; j < series_number_2d; ++j)
            {
                _Type sign = (i + j) % 2 == 0 ? _Type(1) : _Type(-1);
                j_member *= y / j;
                series_sum += sign *  i_member * j_member;
            }
        }
        return series_sum;
    }
};
