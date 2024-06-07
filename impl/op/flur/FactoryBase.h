#ifndef _OP_FLUR_FACTORYBASE__H_
#define _OP_FLUR_FACTORYBASE__H_
#pragma once

namespace OP::flur
{
    /** Base for all classes that composes result Sequence */
    struct FactoryBase {};

    template <class T>
    constexpr static inline bool is_factory_c = std::is_base_of_v<FactoryBase, T>;

}//ns:OP

#endif //_OP_FLUR_FACTORYBASE__H_
