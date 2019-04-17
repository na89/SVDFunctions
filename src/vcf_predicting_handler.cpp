#include "include/vcf_predicting_handler.h"
#include "include/genotype_predictor.h"

namespace vcf {
    void insert(Range r, std::set<Range>& ranges) {
        auto pos = ranges.lower_bound(r);
        if (pos != ranges.end() && pos->includes(r.end() + (-1))) {
            int from = std::min(r.begin().position(), pos->begin().position());
            int to = std::max(r.end().position(), pos->end().position());
            r = Range(r.begin().chromosome(), from, to);
            ranges.erase(*pos);
        }
        ranges.insert(r);
    }

    PredictingHandler::PredictingHandler(const std::vector<std::string>& samples, GenotypeMatrixHandler& gh,
                                         int window_size_kb, int window_size)
                                         :VariantsHandler(samples), gh(gh), curr_chr(-1), iterator{gh},
                                          window(window_size, window_size_kb){
        auto variants = gh.desired_variants();
        int halfws = window_size_kb / 2;
        for (const Variant& v : variants) {
            Chromosome chr = v.position().chromosome();
            int pos = v.position().position();
            Range haplotype{chr, pos - halfws, pos + halfws};
            insert(haplotype, ranges[chr.num()]);
        }
    }

    bool PredictingHandler::isOfInterest(const Variant& variant) {
        Position pos = variant.position();
        auto& set = ranges[pos.chromosome().num()];
        // Workaround, c++11
        auto it = set.lower_bound(Range(pos.chromosome(), -1, pos.position()));
        return it != set.end() && it->includes(pos);
    }

    void PredictingHandler::processVariant(const Variant& variant, const std::vector<Allele>& alleles) {
        std::vector<AlleleType> sample;
        std::for_each(alleles.begin(), alleles.end(), [&sample](const Allele& allele){
            sample.push_back(allele.alleleType());
        });
        Position pos = variant.position();
        if (pos.chromosome() != curr_chr) {
            cleanup();
            curr_chr = pos.chromosome();
        }
        window.add(sample, variant);
    }

    void PredictingHandler::cleanup() {
        for(; iterator.has_next(); ++iterator) {
            Variant var = *iterator;
            auto dataset = window.dataset(var);
        }
    }

    Window::Window(size_t max_size, size_t max_size_kb) :max_size(max_size), max_size_kb(max_size_kb) {}

    void Window::clear() {
        features.clear();
        variants.clear();
        start = 0;
    }

    std::pair<Features, Labels> Window::dataset(const Variant& v) {
        Features fs;
        Labels lbls;
        for (int i = 0; i < variants.size(); i++) {
            if (variants[i] == v) {
                lbls = features[i];
            } else {
                fs.push_back(features[i]);
            }
        }
        if (lbls.empty()) {
            throw std::logic_error("No values for training set. Potentially unreachable code.");
        }
        return {std::move(fs), std::move(lbls)};
    }

    void Window::add(const std::vector<AlleleType>& alleles, const Variant& variant) {
        if (features.size() < max_size) {
            variants.push_back(variant);
            features.push_back(alleles);
        } else {
            variants[start] = variant;
            features[start] = alleles;
            ++start;
            if (start == max_size) {
                start = 0;
            }
        }
    }
}
