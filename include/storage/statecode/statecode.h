#ifndef STATECODE_H
#define STATECODE_H

// 状态码
enum class StateCode
{
    OK,
    ErrNoKey,
    ErrVersion,

    ErrMaybe,

    ErrWrongLeader,
    ErrWrongGroup,
};

#endif