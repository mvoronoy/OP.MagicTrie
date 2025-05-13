#pragma once
#ifndef _OP_FLUR_SEQUENCESTATE__H_
#define _OP_FLUR_SEQUENCESTATE__H_

namespace OP::flur
{
    /** Allows inject additional attributes of pipeline processing
        to functional callback arguments.
    */
    struct SequenceState
    {
        struct Step
        {
            explicit constexpr Step(size_t start = 0) noexcept
                : _step(start) 
            {
            }

            void next() noexcept  { ++_step; }
            constexpr size_t current() const noexcept  { return _step; }
            constexpr operator size_t() const noexcept { return _step; }
            void start() noexcept { _step = 0; }

        private:
            size_t _step;
        };

        /** Advance generation and reset current step to 0*/
        void start() noexcept
        { 
            _stop = false;
            _generation.next();
            _step.start();
        }

        /** Increase current `step` */
        void next() noexcept { _step.next(); }

        const Step& step() const noexcept { return _step; }

        /** \brief count number of times sequence was started (how many `start` method was called) */
        const Step& generation() const noexcept { return _generation; }

        /** Indicate stop the sequence */
        void stop() noexcept
        {
            _stop = true;
        }

        constexpr bool is_stopped() const noexcept
        {
            return _stop;
        }

    private:
        bool _stop = false;
        /** `generation` is an zero based attribute to track how many times 
        *   sequence was started. 
        *  This can be used, for example, to initialize some resources only 
        *  once (by checking if generation == 0).
        */
        Step _generation{ ~size_t(0) };
        
        /** Index of current step of sequence iteration. Each time `Sequence::start` 
        * is invoked it becames zero. `Sequence::next` increases this attribute.
        */ 
        Step _step;
    };

}//ns OP::flur
#endif //_OP_FLUR_SEQUENCESTATE__H_
