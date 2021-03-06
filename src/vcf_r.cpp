#include "vcf_parser.h"
#include <Rcpp.h>
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>
#include <fstream>
#include "zstr/zstr.hpp"
#include "zstr/strict_fstream.hpp"

namespace {
    using namespace Rcpp;
    using namespace vcf;
    using namespace std;
    using boost::algorithm::ends_with;

    class Parser: public VCFParser {
        void handle_error(const vcf::ParserException& e) override {
            Rf_warning(e.get_message().c_str());
        }
    public:
        using VCFParser::VCFParser;
    };

    class RGenotypeMatrixHandler: public GenotypeMatrixHandler {
    public:
        using GenotypeMatrixHandler::GenotypeMatrixHandler;

        IntegerMatrix result() {
            IntegerMatrix res(gmatrix.size(), samples.size());
            for (int i = 0; i < gmatrix.size(); i++) {
                for (int j = 0; j < samples.size(); j++) {
                    int val = gmatrix[i][j];
                    if (val == vcf::MISSING) {
                        val = NA_INTEGER;
                    }
                    res[j * gmatrix.size() + i] = val;
                }
            }
            vector<string> row_names;
            for_each(variants.begin(), variants.end(), [&row_names](Variant& v){
                row_names.push_back((string)v);
            });
            rownames(res) = CharacterVector(row_names.begin(), row_names.end());
            return res;
        }
    };

    class RCallRateHandler: public CallRateHandler {
    public:
        using CallRateHandler::CallRateHandler;

        NumericMatrix result() {
            NumericMatrix result(ranges.size(), samples.size());
            for (int i = 0; i < ranges.size(); i++) {
                for (int j = 0; j < samples.size(); j++) {
                    result[j * ranges.size() + i] = (double)call_rate_matrix[i][j] / n_variants[i];
                }
            }
            return result;
        }
    };
}

VCFFilter filter(const CharacterVector& samples, const CharacterVector& bad_positions,
        const CharacterVector& variants, int DP, int GQ) {
    VCFFilter filter(DP, GQ);

    if (samples.length() > 0) {
        vector<string> ss;
        for_each(samples.begin(), samples.end(), [&ss](const char *s) { ss.emplace_back(s); });
        filter.add_samples(ss);
    }

    if (bad_positions.length() > 0) {
        vector<Position> bads;
        for_each(bad_positions.begin(), bad_positions.end(), [&bads](const char *s) {
            bads.push_back(Position::parse_position(string(s)));
        });
        filter.add_bad_variants(bads);
    }

    if (variants.length() > 0) {
        vector<Variant> vs;
        for_each(variants.begin(), variants.end(), [&vs](const char *s) {
            vector<Variant> variants = Variant::parseVariants(string(s));
            vs.insert(vs.end(), variants.begin(), variants.end());
        });
        filter.set_available_variants(vs);
    }
    return filter;
}

vector<vcf::Range> parse_regions(const CharacterVector& regions){
    vector<vcf::Range> ranges;
    for_each(regions.begin(), regions.end(), [&ranges](const char* str){
        ranges.push_back(vcf::Range::parseRange(string(str)));
    });
    return ranges;
}

// [[Rcpp::export]]
List parse_vcf(const CharacterVector& filename, const CharacterVector& samples,
               const CharacterVector& bad_positions, const CharacterVector& allowed_variants,
               const IntegerVector& DP, const IntegerVector& GQ, const CharacterVector& regions,
               const LogicalVector& ret_gmatrix, const CharacterVector& binary_prefix) {
    List ret;
    try {
        const char *name = filename[0];
        unique_ptr<std::istream> in(new zstr::ifstream(name));

        Parser parser(*in, filter(samples, bad_positions, allowed_variants, DP[0], GQ[0]));
        parser.parse_header();
        auto ss = parser.sample_names();
        shared_ptr<RGenotypeMatrixHandler> gmatrix_handler;
        shared_ptr<BinaryFileHandler> binary_handler;
        shared_ptr<RCallRateHandler> callrate_handler;

        if (ret_gmatrix[0]) {
            gmatrix_handler.reset(new RGenotypeMatrixHandler(ss));
            parser.register_handler(gmatrix_handler);
        }

        if (regions.length() > 0) {
            callrate_handler.reset(new RCallRateHandler(ss, parse_regions(regions)));
            parser.register_handler(callrate_handler);
        }

        if (binary_prefix.length() > 0) {
            string prefix = string(binary_prefix[0]);
            binary_handler.reset(new BinaryFileHandler(ss, prefix + "_bin", prefix + "_meta"));
            parser.register_handler(binary_handler);
        }

        parser.parse_genotypes();
        ret["samples"] = CharacterVector(ss.begin(), ss.end());
        if (ret_gmatrix[0]) {
            ret["genotype"] = gmatrix_handler->result();
        }
        if (regions.length() > 0) {
            ret["callrate"] = callrate_handler->result();
        }
    } catch (ParserException& e) {
        ::Rf_error(e.get_message().c_str());
    }
    return ret;
}
