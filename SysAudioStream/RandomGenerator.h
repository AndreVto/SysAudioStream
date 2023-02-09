#pragma once

#include <random>

class RandomGenerator
{
    private:
        std::random_device m_rd;
        std::mt19937 m_mte;

    public:
        RandomGenerator() 
        { 
            std::mt19937 local_mte(m_rd());
            m_mte = local_mte; 
        }

        void Generate(byte* buffer, int len)
        {
            std::uniform_int_distribution<int> dist(0, 255);

            for (int i = 0; i < len; i++) {
                buffer[i] = dist(m_mte);
            }
        }
    };