// Copyright(c) 2015-2026 Panos Karabelas. Distributed under the MIT license.
#pragma once
#include <vector>
#include <utility>
#include <cstddef>

namespace spartan::road_guardrail
{
    // Inclusive station ranges. An excluded station is a hard break, even when
    // extending approaches or joining two short gaps in the hazard mask.
    inline std::vector<std::pair<size_t,size_t>> SelectRuns(const std::vector<bool>& hazard,
        const std::vector<bool>& allowed, float spacing, float approach, float gap, float minimum)
    {
        std::vector<std::pair<size_t,size_t>> runs;
        if (hazard.size()!=allowed.size() || spacing<=0) return runs;
        const size_t count=hazard.size();
        std::vector<bool> selected(count,false);
        const size_t extension=static_cast<size_t>(approach/spacing);
        for (size_t i=0;i<count;++i) if (hazard[i] && allowed[i])
        {
            selected[i]=true;
            for (size_t j=1;j<=extension && j<=i && allowed[i-j];++j) selected[i-j]=true;
            for (size_t j=1;j<=extension && i+j<count && allowed[i+j];++j) selected[i+j]=true;
        }
        for (size_t i=1;i<count;)
        {
            if (selected[i] || !selected[i-1]) {++i;continue;}
            const size_t start=i;
            while (i<count && allowed[i] && !selected[i]) ++i;
            if (i<count && selected[i] && (i-start)*spacing<=gap)
                for (size_t j=start;j<i;++j) selected[j]=true;
            if (i==start) ++i;
        }
        for (size_t i=0;i<count;)
        {
            if (!selected[i]) {++i;continue;}
            const size_t first=i;
            while (i<count && selected[i]) ++i;
            if ((i-1-first)*spacing>=minimum) runs.emplace_back(first,i-1);
        }
        return runs;
    }
}
