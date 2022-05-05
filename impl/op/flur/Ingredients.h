#pragma once
#ifndef _OP_FLUR_INGREDIENTS__H_
#define _OP_FLUR_INGREDIENTS__H_

namespace OP::flur
{
    /** Allows inject additional attributes of pipeline processing
        to functional callback arguments.
    */
    struct PipelineAttrs
    {
        struct Step
        {
            Step() :_step(0) {}
            void next() { ++_step; }
            size_t current() const { return _step; }
            void start() { _step = 0; }
        private:
            size_t _step;
        };
        Step _step, _generation;
    };

}//ns OP::flur
#endif //_OP_FLUR_INGREDIENTS__H_