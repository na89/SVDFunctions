#include "include/vcf_parser.h"

#include <algorithm>
#include <sstream>
#include <iostream>
#include <Rcpp.h>

namespace {
    using namespace vcf;

    using std::istream;
    using std::vector;
    using std::string;
    using std::find;
    using std::pair;
    using std::stoi;

    // Rcpp conflict
    using vcf::MISSING;

    vector<std::string> split(const string& line, char delim, int max_num_tokens = 0){
        unsigned long tokens = 1;
        for (char ch: line) {
            if (ch == delim) {
                ++tokens;
                if (tokens == max_num_tokens) {
                    break;
                }
            }
        }

        vector<string> result;
        result.reserve(tokens);
        unsigned long last = 0;
        for (int i = 0; i < line.length(); i++) {
            char ch = line[i];
            if (ch == delim) {
                result.push_back(line.substr(last, i - last));
                last = i + 1;
                if (result.size() == max_num_tokens) {
                    return result;
                }
            }
        }

        if (last != line.length()) {
            result.push_back(line.substr(last, line.length() - last));
        }

        return result;
    }

    Position parse_position(const vector<string>& tokens) {
        Chromosome chr(tokens[CHROM]);
        int pos;
        try {
            pos = stoi(tokens[POS]);
        } catch (...) {
            throw ParserException("Can't read variant position");
        }
        return {chr, pos};
    }

    class Format {
        const string DP_FIELD = "DP";
        const string GQ_FIELD = "GQ";
        const string GT_FIELD = "GT" ;
        const string AD_FIELD = "AD";

        const char DELIM_1 = '|';
        const char DELIM_2 = '/';

        long depth_pos;
        long qual_pos;
        long genotype_pos;
        long ad_pos;

        void find_pos(const vector<string>& tokens, const string& field, long& pos) {
            auto position = find(tokens.begin(), tokens.end(), field);
            if (position == tokens.end()) {
                pos = -1;
            } else {
                pos = position - tokens.begin();
            }
        }

        AlleleType type(int first, int second, int allele) {
            if (first > second) {
                std::swap(first, second);
            }
            if (first == second) {
                if (first == 0) {
                    return HOMREF;
                } else if (first == allele) {
                    return HOM;
                }
            } else {
                if (first == 0 && second == allele) {
                    return HET;
                }
            }
            return MISSING;
        }

    public:
        Format(const string& format) {
            vector<string> parts = split(format, ':');
            find_pos(parts, DP_FIELD, depth_pos);
            find_pos(parts, GQ_FIELD, qual_pos);
            find_pos(parts, AD_FIELD, ad_pos);
            find_pos(parts, GT_FIELD, genotype_pos);
            if (genotype_pos == -1) {
                throw ParserException("No GT field available for a variant");
            }
        }

        AlleleType parse_gt(const string& gt, int allele){
            if (allele == 0) {
                return HOM;
            }
            int first_allele, second_allele;
            std::istringstream iss(gt);
            iss >> first_allele;
            if (iss.eof()) {
                if (first_allele == 0) {
                    return HOMREF;
                }
                return first_allele == allele ? HOM : MISSING;
            }
            char ch;
            iss >> ch;
            if (ch != DELIM_1 && ch != DELIM_2) {
                throw ParserException("Wrong GT format: " + gt);
            }
            iss >> second_allele;
            if (iss.fail()) {
                throw ParserException("Wrong GT format: " + gt);
            }
            return type(first_allele, second_allele, allele);
        }

        Allele parse(const string& genotype, int allele, const VCFFilter& filter, VCFFilterStats& stats) {
            vector<string> parts = split(genotype, ':');
            try {
                string gt = parts[genotype_pos];
                if (gt == "." || gt == "./." || gt == ".|.") {
                    stats.add(Stat::GT_MISS, 1);
                    return {MISSING, 0, 0};
                }

                int dp = depth_pos == -1 || parts[depth_pos] == "." ? 0 : stoi(parts[depth_pos]);
                int gq = qual_pos == -1 || parts[qual_pos] == "." ? 0 : stoi(parts[qual_pos]);

                if (!filter.apply(dp, gq)) {
                    stats.add(Stat::DP_GQ, 1);
                    return {MISSING, (unsigned)dp, (unsigned)gq};
                }
                Allele ret{parse_gt(gt, allele), (unsigned)dp, (unsigned)gq};
                if (ret.alleleType() == HET) {
                    if (ad_pos != -1 && dp != 0) {
                        std::istringstream adstream(parts[ad_pos]);
                        int ref, alt;
                        char ch;
                        adstream >> ref;
                        for (int i = 0; i < allele; i++) {
                            while(!std::isdigit(adstream.peek())) {
                                adstream >> ch;
                            }
                            adstream  >> alt;
                        }
                        if (!adstream.fail()) {
                            double ref_ratio = ref / (double)dp;
                            double alt_ratio = alt / (double)dp;
                            if (ref_ratio < 0.3 || ref_ratio > 0.7) {
                                stats.add(Stat::ALLELE_BALANCE, 1);
                                return {MISSING, 0, 0};
                            }
                            if (alt_ratio < 0.3 || alt_ratio > 0.7) {
                                stats.add(Stat::ALLELE_BALANCE, 1);
                                return {MISSING, 0, 0};
                            }
                        }
                    }
                }
                return ret;
            } catch (...) {
                throw ParserException("Wrong GT format: " + genotype);
            }
        }
    };
}

namespace vcf {

    void VCFParser::register_handler(std::shared_ptr<VariantsHandler> handler, int order) {
        int i_ins = 0;
        for (int i = 0; i < handlers.size(); i++) {
            if (handlers[i].second >= order) {
                i_ins = i;
            }
        }
        handlers.emplace_back(handler, order);
        std::swap(handlers[handlers.size() - 1], handlers[i_ins]);
    }

    VCFParser::VCFParser(std::istream& input, const VCFFilter& filter, VCFFilterStats& stats) :filter(filter),
                         input(input), line_num(0), stats(stats){}

    std::vector<std::string> VCFParser::sample_names() {
        return samples;
    }

    vector<Variant> VCFParser::parse_variants(const vector<string>& tokens, const Position& position) {
        vector<Variant> variants;
        string ref = tokens[REF];
        vector<string> alts = split(tokens[ALT], ',');
        for (const string& alt: alts) {
            Variant variant(position, ref, alt);
            if (is_of_interest(variant)) {
                variants.emplace_back(position, ref, alt);
            }
        }
        return variants;
    }

    void VCFParser::parse_header() {
        string line;
        while (getline(input, line)) {
            ++line_num;
            if (line.substr(0, 2) == "##") {
                continue;
            }
            if (line.substr(0, 1) == "#") {
                line = line.substr(1);
                auto tokens = split(line, DELIM);
                number_of_samples = tokens.size() - FIELDS.size();
                for (int i = 0; i < tokens.size(); i++) {
                    const string& token = tokens[i];
                    if (i < FIELDS.size()) {
                        if (token != FIELDS[i]) {
                            throw ParserException("Wrong header line: expected column " + FIELDS[i] +
                                                  "Found: " + token, line_num);
                        }
                    } else {
                        if (filter.apply(token)) {
                            samples.push_back(token);
                            filtered_samples.push_back(i);
                        }
                    }
                }
                return;
            }
        }
        throw ParserException("No VCF header found in given file");
    }

    bool VCFParser::is_of_interest(const Variant& var) {
        bool interesting = false;
        for (const auto& handler: handlers) {
            interesting |= handler.first->isOfInterest(var);
        }
        return interesting;
    }

    void VCFParser::parse_genotypes() {
        string line;
        while (getline(input, line)) {
            Rcpp::checkUserInterrupt();
            ++line_num;
            if (line.empty() || std::all_of(line.begin(),line.end(),isspace)) {
                continue;
            }
            vector<string> tokens = split(line, DELIM, FIELDS.size());
            try {
                if (tokens.size() < FIELDS.size()) {
                    throw ParserException("The row is too short");
                }
                Position position = parse_position(tokens);
                vector<Variant> variants = parse_variants(tokens, position);
                stats.add(Stat::OVERALL, variants.size());

                if (tokens[FILTER] != "PASS") {
                    stats.add(Stat::NON_PASS, variants.size());
                    continue;
                }

                if (!filter.apply(position)) {
                    stats.add(Stat::BANNED, variants.size());
                    continue;
                }

                if (variants.empty()) {
                    continue;
                }
                tokens = split(line, DELIM);

                if (tokens.size() != FIELDS.size() + number_of_samples) {
                    stats.add(Stat::WARNING, variants.size());
                    throw ParserException("The row has " + std::to_string(tokens.size()) +
                                          " number of columns whereas header has " + std::to_string(FIELDS.size() + samples.size()));
                }

                Format format(tokens[FORMAT]);

                for (int i = 0; i < variants.size(); i++) {
                    Variant& variant = variants[i];
                    vector<Allele> alleles;
                    for (int sample : filtered_samples) {
                        alleles.push_back(format.parse(tokens.at(sample), i + 1, filter, stats));
                    }
                    for (auto& handler: handlers) {
                        handler.first->processVariant(variant, alleles);
                    }
                }
            } catch (const ParserException& e) {
                ParserException exception(e.get_message(), line_num);
                handle_error(exception);
            }
        }
    }
}
