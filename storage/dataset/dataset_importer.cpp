#include "storage/dataset/dataset_importer.hpp"
#include "storage/common/file_system.hpp"

#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <charconv>

namespace obsidian {
namespace storage {
namespace dataset {

namespace {

struct ParsedBar {
    int64_t timestamp_ns{0};
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double volume{0.0};
    double vwap{0.0};
};

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '"')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' || s.back() == '"')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> SplitLine(std::string_view line, char delimiter) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        size_t end = line.find(delimiter, start);
        if (end == std::string_view::npos) {
            tokens.push_back(line.substr(start));
            break;
        }
        tokens.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    if (!line.empty() && line.back() == delimiter) {
        tokens.emplace_back("");
    }
    return tokens;
}

} // namespace

StatusOr<int64_t> DatasetImporter::ParseIso8601ToNs(std::string_view iso_str) {
    iso_str = Trim(iso_str);
    if (iso_str.size() < 10) {
        return Status::InvalidArgument("Invalid ISO-8601 string: too short: " + std::string(iso_str));
    }

    // Expecting YYYY-MM-DD...
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0, millis = 0;

    auto res = std::from_chars(iso_str.data(), iso_str.data() + 4, year);
    if (res.ec != std::errc() || iso_str[4] != '-') {
        return Status::InvalidArgument("Failed to parse year in ISO-8601 string: " + std::string(iso_str));
    }
    res = std::from_chars(iso_str.data() + 5, iso_str.data() + 7, month);
    if (res.ec != std::errc() || iso_str[7] != '-') {
        return Status::InvalidArgument("Failed to parse month in ISO-8601 string: " + std::string(iso_str));
    }
    res = std::from_chars(iso_str.data() + 8, iso_str.data() + 10, day);
    if (res.ec != std::errc()) {
        return Status::InvalidArgument("Failed to parse day in ISO-8601 string: " + std::string(iso_str));
    }

    if (iso_str.size() >= 19 && (iso_str[10] == 'T' || iso_str[10] == ' ')) {
        std::from_chars(iso_str.data() + 11, iso_str.data() + 13, hour);
        std::from_chars(iso_str.data() + 14, iso_str.data() + 16, minute);
        std::from_chars(iso_str.data() + 17, iso_str.data() + 19, second);
        if (iso_str.size() >= 23 && iso_str[19] == '.') {
            std::from_chars(iso_str.data() + 20, iso_str.data() + 23, millis);
        }
    }

    // Howard Hinnant algorithm for civil calendar days
    int64_t y = year;
    if (month <= 2) y -= 1;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + doe - 719468;

    int64_t total_sec = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    int64_t total_ns = total_sec * 1000000000LL + millis * 1000000LL;

    return total_ns;
}

StatusOr<IngestionResult> DatasetImporter::ImportCsv(
    const std::string& csv_path,
    int32_t symbol_id,
    int32_t resolution_ms,
    bool quarantine_invalid_rows) {
    auto fs = LocalFileSystem::Default();
    auto text_or = fs->ReadAllText(csv_path);
    if (!text_or.ok()) {
        return text_or.status();
    }
    return ImportCsvContent(text_or.value(), symbol_id, resolution_ms, quarantine_invalid_rows);
}

StatusOr<IngestionResult> DatasetImporter::ImportCsvContent(
    std::string_view csv_content,
    int32_t symbol_id,
    int32_t resolution_ms,
    bool quarantine_invalid_rows) {
    IngestionResult result;
    if (csv_content.empty()) {
        return Status::InvalidArgument("CSV content is empty");
    }

    char delimiter = ';';
    size_t first_line_end = csv_content.find('\n');
    std::string_view header_line = csv_content.substr(0, first_line_end);
    if (header_line.find(';') == std::string_view::npos && header_line.find(',') != std::string_view::npos) {
        delimiter = ',';
    }

    auto header_tokens = SplitLine(header_line, delimiter);
    int ts_col = -1, open_col = -1, high_col = -1, low_col = -1, close_col = -1, vol_col = -1;

    for (size_t i = 0; i < header_tokens.size(); ++i) {
        std::string token(Trim(header_tokens[i]));
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (token == "timeopen" || token == "timestamp" || token == "date" || token == "time") {
            if (ts_col == -1) ts_col = static_cast<int>(i);
        } else if (token == "open") {
            open_col = static_cast<int>(i);
        } else if (token == "high") {
            high_col = static_cast<int>(i);
        } else if (token == "low") {
            low_col = static_cast<int>(i);
        } else if (token == "close") {
            close_col = static_cast<int>(i);
        } else if (token == "volume") {
            vol_col = static_cast<int>(i);
        }
    }

    if (ts_col == -1 || open_col == -1 || high_col == -1 || low_col == -1 || close_col == -1) {
        return Status::InvalidArgument("Missing required OHLCV columns in CSV header");
    }

    std::vector<ParsedBar> parsed_bars;
    size_t line_start = (first_line_end == std::string_view::npos) ? csv_content.size() : first_line_end + 1;

    int64_t prev_row_ts = -1;

    while (line_start < csv_content.size()) {
        size_t line_end = csv_content.find('\n', line_start);
        std::string_view line = (line_end == std::string_view::npos) 
            ? csv_content.substr(line_start) 
            : csv_content.substr(line_start, line_end - line_start);
        line_start = (line_end == std::string_view::npos) ? csv_content.size() : line_end + 1;

        std::string_view trimmed_line = Trim(line);
        if (trimmed_line.empty()) {
            continue;
        }

        result.total_rows_read++;
        auto tokens = SplitLine(line, delimiter);
        int max_col = std::max({ts_col, open_col, high_col, low_col, close_col, vol_col});
        if (static_cast<int>(tokens.size()) <= max_col) {
            result.quarantined_rows++;
            continue;
        }

        auto ts_or = ParseIso8601ToNs(tokens[ts_col]);
        if (!ts_or.ok()) {
            result.quarantined_rows++;
            continue;
        }
        int64_t ts_ns = ts_or.value();

        if (prev_row_ts != -1 && ts_ns < prev_row_ts) {
            result.anomalies.temporal_regression_count++;
        }
        prev_row_ts = ts_ns;

        try {
            double o = std::stod(std::string(Trim(tokens[open_col])));
            double h = std::stod(std::string(Trim(tokens[high_col])));
            double l = std::stod(std::string(Trim(tokens[low_col])));
            double c = std::stod(std::string(Trim(tokens[close_col])));
            double v = (vol_col >= 0 && vol_col < static_cast<int>(tokens.size())) 
                ? std::stod(std::string(Trim(tokens[vol_col]))) 
                : 0.0;

            // Anomaly Checks
            bool is_anomaly = false;
            if (std::isnan(o) || std::isnan(h) || std::isnan(l) || std::isnan(c) || std::isnan(v) ||
                std::isinf(o) || std::isinf(h) || std::isinf(l) || std::isinf(c) || std::isinf(v)) {
                result.anomalies.nan_or_inf_count++;
                is_anomaly = true;
            }
            if (o <= 0.0 || h <= 0.0 || l <= 0.0 || c <= 0.0) {
                result.anomalies.negative_or_zero_price_count++;
                is_anomaly = true;
            }
            if (v < 0.0) {
                result.anomalies.negative_volume_count++;
                is_anomaly = true;
            }
            if (h < l) {
                result.anomalies.high_less_than_low_count++;
                is_anomaly = true;
            }
            if (o > h || o < l || c > h || c < l) {
                result.anomalies.open_close_out_of_bounds_count++;
                is_anomaly = true;
            }

            if (is_anomaly) {
                if (quarantine_invalid_rows) {
                    result.quarantined_rows++;
                    continue;
                } else {
                    return Status::Corruption("Financial anomaly error detected in market bar at timestamp " + std::to_string(ts_ns));
                }
            }

            ParsedBar bar;
            bar.timestamp_ns = ts_ns;
            bar.open = o;
            bar.high = h;
            bar.low = l;
            bar.close = c;
            bar.volume = v;
            bar.vwap = (o + h + l + c) / 4.0;
            parsed_bars.push_back(bar);
            result.valid_rows++;

        } catch (const std::exception&) {
            result.quarantined_rows++;
        }
    }

    if (parsed_bars.empty()) {
        return Status::InvalidArgument("No valid OHLCV rows could be parsed from dataset");
    }

    // Sort chronologically ascending
    std::sort(parsed_bars.begin(), parsed_bars.end(), [](const ParsedBar& a, const ParsedBar& b) {
        return a.timestamp_ns < b.timestamp_ns;
    });

    result.min_timestamp_ns = parsed_bars.front().timestamp_ns;
    result.max_timestamp_ns = parsed_bars.back().timestamp_ns;

    // Build ColumnarBatch
    size_t count = parsed_bars.size();
    std::vector<int64_t> bar_ts(count);
    std::vector<int32_t> sym_ids(count, symbol_id);
    std::vector<int32_t> res_ms(count, resolution_ms);
    std::vector<double> opens(count), highs(count), lows(count), closes(count), volumes(count), vwaps(count);
    std::vector<int64_t> tick_counts(count, 1);
    std::vector<double> buy_vols(count), sell_vols(count);

    for (size_t i = 0; i < count; ++i) {
        bar_ts[i] = parsed_bars[i].timestamp_ns;
        opens[i] = parsed_bars[i].open;
        highs[i] = parsed_bars[i].high;
        lows[i] = parsed_bars[i].low;
        closes[i] = parsed_bars[i].close;
        volumes[i] = parsed_bars[i].volume;
        vwaps[i] = parsed_bars[i].vwap;
        buy_vols[i] = parsed_bars[i].volume * 0.5;
        sell_vols[i] = parsed_bars[i].volume * 0.5;
    }

    auto schema = schema::BarOHLCV::GetSchema();
    std::vector<std::shared_ptr<arrow::IColumnVector>> cols;
    cols.push_back(std::make_shared<arrow::ColumnVector<int64_t>>(schema::DataType::TimestampNs, bar_ts.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<int32_t>>(schema::DataType::Int32, sym_ids.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<int32_t>>(schema::DataType::Int32, res_ms.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, opens.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, highs.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, lows.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, closes.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, volumes.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, vwaps.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<int64_t>>(schema::DataType::Int64, tick_counts.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, buy_vols.data(), count));
    cols.push_back(std::make_shared<arrow::ColumnVector<double>>(schema::DataType::Double, sell_vols.data(), count));

    result.batch = std::make_shared<arrow::ColumnarBatch>(schema, std::move(cols), count);
    return result;
}

} // namespace dataset
} // namespace storage
} // namespace obsidian
