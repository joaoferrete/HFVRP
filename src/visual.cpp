#include "visual.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace hfvrp {

namespace {

// Paleta ColorBrewer, suficiente para 15 rotas distintas.
const char* kVehicleColors[] = {
    "#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00",
    "#a65628", "#f781bf", "#999999", "#66c2a5", "#fc8d62",
    "#8da0cb", "#e78ac3", "#a6d854", "#ffd92f", "#e5c494",
};
constexpr int kNumColors = sizeof(kVehicleColors) / sizeof(kVehicleColors[0]);

// Converte a prioridade em uma cor, do cinza claro ao vermelho.
std::string priority_fill(double p, double max_p) {
    if (max_p < 1e-9 || p < 1e-9) return "#eeeeee";
    const double t = std::min(1.0, p / max_p);
    const int r = 238 + (int)std::round((215 - 238) * t); //  238 -> 215
    const int g = 238 + (int)std::round(( 48 - 238) * t); //  238 -> 48
    const int b = 238 + (int)std::round(( 39 - 238) * t); //  238 -> 39
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return buf;
}

std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;";break;
            default:  out += c;
        }
    }
    return out;
}

std::string fmt_num(double v, int prec = 2) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

} // namespace

void write_svg(const Instance& inst,
               const Solution& sol,
               double beta,
               const std::string& method,
               const std::string& path,
               const VisualExtras& extras) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write SVG to " + path);

    // Geometria do desenho.
    constexpr int W       = 1100;
    constexpr int H       = 820;
    constexpr int HEADER  = 56;
    constexpr int FOOTER  = 110;
    constexpr int SIDEBAR = 320;
    constexpr int MARGIN  = 30;
    const double plot_x0 = MARGIN;
    const double plot_x1 = W - SIDEBAR - MARGIN;
    const double plot_y0 = HEADER + MARGIN;
    const double plot_y1 = H - FOOTER - MARGIN;

    // Retangulo que envolve as coordenadas, com uma folga.
    double xmin = inst.coord_x[0], xmax = inst.coord_x[0];
    double ymin = inst.coord_y[0], ymax = inst.coord_y[0];
    for (int i = 0; i <= inst.num_customers; ++i) {
        xmin = std::min(xmin, inst.coord_x[i]);
        xmax = std::max(xmax, inst.coord_x[i]);
        ymin = std::min(ymin, inst.coord_y[i]);
        ymax = std::max(ymax, inst.coord_y[i]);
    }
    const double xrange = std::max(1.0, xmax - xmin) * 1.05;
    const double yrange = std::max(1.0, ymax - ymin) * 1.05;
    const double xc = 0.5 * (xmin + xmax);
    const double yc = 0.5 * (ymin + ymax);

    auto tx = [&](double x) {
        return plot_x0 + (x - (xc - xrange / 2)) / xrange * (plot_x1 - plot_x0);
    };
    auto ty = [&](double y) {
        // Inverte o eixo Y para que coordenadas maiores figuem acima.
        return plot_y1 - (y - (yc - yrange / 2)) / yrange * (plot_y1 - plot_y0);
    };

    // Prioridade maxima, usada na escala de cor.
    double max_p = 0.0;
    for (int i = 1; i <= inst.num_customers; ++i)
        max_p = std::max(max_p, inst.priority[i]);

    // Demanda maxima, usada na escala de raio.
    double max_d = 1.0;
    for (int i = 1; i <= inst.num_customers; ++i)
        max_d = std::max(max_d, inst.demand[i]);

    // Estatisticas de uso da frota.
    int vehicles_used = 0;
    std::vector<int> idle_vehicle_ids;    // 0-based indices
    double total_load = 0.0;
    double total_capacity_used = 0.0;     // capacities of used vehicles only
    for (const auto& r : sol.routes) {
        if (!r.customers.empty() && r.vehicle_id >= 0) {
            ++vehicles_used;
            total_capacity_used += inst.vehicles[r.vehicle_id].capacity;
            for (int c : r.customers) total_load += inst.demand[c];
        }
    }
    for (int k = 0; k < inst.num_vehicles; ++k) {
        bool in_use = false;
        for (const auto& r : sol.routes)
            if (r.vehicle_id == k && !r.customers.empty()) { in_use = true; break; }
        if (!in_use) idle_vehicle_ids.push_back(k);
    }
    const int vehicles_idle = (int)idle_vehicle_ids.size();
    const double fleet_util = (inst.num_vehicles > 0)
                                  ? (100.0 * vehicles_used / inst.num_vehicles)
                                  : 0.0;
    const double cap_util = (total_capacity_used > 0.0)
                                ? (100.0 * total_load / total_capacity_used)
                                : 0.0;

    out << std::fixed << std::setprecision(2);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << W << " " << H
        << "\" font-family=\"Helvetica,Arial,sans-serif\" font-size=\"12\">\n";

    // Fundo.
    out << "<rect width=\"" << W << "\" height=\"" << H << "\" fill=\"#ffffff\"/>\n";

    // Ponta de seta, uma por cor de veiculo.
    out << "<defs>\n";
    for (int k = 0; k < kNumColors; ++k) {
        out << "  <marker id=\"arrow_" << k
            << "\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"8\" "
            << "markerHeight=\"8\" orient=\"auto-start-reverse\">\n"
            << "    <path d=\"M0,0 L10,5 L0,10 z\" fill=\"" << kVehicleColors[k] << "\"/>\n"
            << "  </marker>\n";
    }
    out << "</defs>\n";

    // Cabecalho.
    std::ostringstream header;
    header << "HFVRP-P — " << inst.name
           << "  |  method=" << method
           << "  |  β=" << beta
           << "  |  N=" << inst.num_customers
           << "  |  M=" << inst.num_vehicles
           << "  |  " << (sol.feasible ? "feasible" : "INFEASIBLE");
    out << "<text x=\"" << MARGIN << "\" y=\"30\" font-size=\"16\" font-weight=\"600\">"
        << xml_escape(header.str()) << "</text>\n";

    // Moldura da area de desenho.
    out << "<rect x=\"" << plot_x0 << "\" y=\"" << plot_y0
        << "\" width=\"" << (plot_x1 - plot_x0) << "\" height=\"" << (plot_y1 - plot_y0)
        << "\" fill=\"#fafafa\" stroke=\"#cccccc\" stroke-width=\"1\"/>\n";

    // Rotas, desenhadas atras dos marcadores de cliente.
    int color_idx = 0;
    for (const auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        const char* col = kVehicleColors[color_idx % kNumColors];
        int prev = 0;
        for (size_t step = 0; step < r.customers.size(); ++step) {
            const int c = r.customers[step];
            out << "<line x1=\"" << tx(inst.coord_x[prev])
                << "\" y1=\"" << ty(inst.coord_y[prev])
                << "\" x2=\"" << tx(inst.coord_x[c])
                << "\" y2=\"" << ty(inst.coord_y[c])
                << "\" stroke=\"" << col << "\" stroke-width=\"1.8\" opacity=\"0.8\""
                << " marker-end=\"url(#arrow_" << (color_idx % kNumColors) << ")\"/>\n";
            prev = c;
        }
        // Trecho de retorno ao deposito.
        out << "<line x1=\"" << tx(inst.coord_x[prev])
            << "\" y1=\"" << ty(inst.coord_y[prev])
            << "\" x2=\"" << tx(inst.coord_x[0])
            << "\" y2=\"" << ty(inst.coord_y[0])
            << "\" stroke=\"" << col << "\" stroke-width=\"1.8\" opacity=\"0.8\""
            << " stroke-dasharray=\"4 3\""
            << " marker-end=\"url(#arrow_" << (color_idx % kNumColors) << ")\"/>\n";
        ++color_idx;
    }

    // Marcador do deposito.
    const double dx = tx(inst.coord_x[0]);
    const double dy = ty(inst.coord_y[0]);
    out << "<rect x=\"" << (dx - 8) << "\" y=\"" << (dy - 8)
        << "\" width=\"16\" height=\"16\" fill=\"#222222\" stroke=\"#000000\"/>\n";
    out << "<text x=\"" << dx << "\" y=\"" << (dy - 12)
        << "\" text-anchor=\"middle\" fill=\"#222\">depot</text>\n";

    // Clientes.
    for (int i = 1; i <= inst.num_customers; ++i) {
        const double x = tx(inst.coord_x[i]);
        const double y = ty(inst.coord_y[i]);
        const double r = 6.0 + 6.0 * (inst.demand[i] / max_d);
        out << "<circle cx=\"" << x << "\" cy=\"" << y
            << "\" r=\"" << r << "\" fill=\"" << priority_fill(inst.priority[i], max_p)
            << "\" stroke=\"#333\" stroke-width=\"1\"/>\n";
        out << "<text x=\"" << x << "\" y=\"" << (y + 3.5)
            << "\" text-anchor=\"middle\" font-size=\"10\" fill=\"#000\">" << i
            << "</text>\n";
    }

    // Painel lateral.
    const double sb_x = plot_x1 + MARGIN;
    double row_y = plot_y0 + 4;

    // Bloco de resumo.
    out << "<text x=\"" << sb_x << "\" y=\"" << row_y
        << "\" font-weight=\"600\">Summary</text>\n";
    row_y += 18;

    auto put_line = [&](const std::string& s) {
        out << "<text x=\"" << sb_x << "\" y=\"" << row_y << "\">"
            << xml_escape(s) << "</text>\n";
        row_y += 16;
    };
    auto put_line_muted = [&](const std::string& s) {
        out << "<text x=\"" << sb_x << "\" y=\"" << row_y << "\" fill=\"#555\">"
            << xml_escape(s) << "</text>\n";
        row_y += 16;
    };

    {
        std::ostringstream s;
        s << "runtime = " << fmt_num(extras.runtime_sec, 3) << " s";
        put_line(s.str());
    }
    {
        std::ostringstream s;
        s << "vehicles used = " << vehicles_used << " / " << inst.num_vehicles
          << "  (" << fmt_num(fleet_util, 1) << "%)";
        put_line(s.str());
    }
    {
        std::ostringstream s;
        s << "vehicles idle = " << vehicles_idle;
        put_line(s.str());
    }
    if (!idle_vehicle_ids.empty()) {
        std::ostringstream s;
        s << "  idle IDs: ";
        for (size_t i = 0; i < idle_vehicle_ids.size(); ++i) {
            if (i) s << ", ";
            s << "V" << (idle_vehicle_ids[i] + 1);
        }
        put_line_muted(s.str());
    }
    {
        std::ostringstream s;
        s << "total demand served = " << fmt_num(total_load, 0);
        put_line(s.str());
    }
    if (total_capacity_used > 0.0) {
        std::ostringstream s;
        s << "capacity utilisation = " << fmt_num(cap_util, 1) << "%"
          << "  (" << fmt_num(total_load, 0) << " / "
          << fmt_num(total_capacity_used, 0) << ")";
        put_line(s.str());
    }

    // Bloco exclusivo do modelo exato.
    if (extras.exact) {
        row_y += 6;
#if defined(HFVRP_USE_CPLEX)
        const char* exact_label = "Exact (CPLEX)";
#else
        const char* exact_label = "Exact (CBC)";
#endif
        out << "<text x=\"" << sb_x << "\" y=\"" << row_y
            << "\" font-weight=\"600\">" << exact_label << "</text>\n";
        row_y += 18;

        {
            std::ostringstream s;
            s << "status = " << extras.status
              << (extras.optimal ? "  (proven optimal)" : "");
            put_line(s.str());
        }
        if (sol.feasible) {
            std::ostringstream s;
            s << "UB (incumbent) = " << fmt_num(sol.cost_total, 2);
            put_line(s.str());
        }
        {
            std::ostringstream s;
            s << "LB (best bound) = " << fmt_num(extras.lower_bound, 2);
            put_line(s.str());
        }
        {
            std::ostringstream s;
            s << "root LP bound = " << fmt_num(extras.root_lp_bound, 2);
            put_line(s.str());
        }
        {
            std::ostringstream s;
            if (!sol.feasible) s << "gap = n/a (no incumbent)";
            else               s << "gap = " << fmt_num(100.0 * extras.gap, 2) << " %";
            put_line(s.str());
        }
        {
            std::ostringstream s;
            s << "B&B nodes = " << extras.num_nodes;
            put_line(s.str());
        }
    }

    // Bloco com as rotas.
    row_y += 8;
    out << "<text x=\"" << sb_x << "\" y=\"" << row_y
        << "\" font-weight=\"600\">Routes</text>\n";
    row_y += 20;
    color_idx = 0;
    for (const auto& r : sol.routes) {
        if (r.customers.empty() || r.vehicle_id < 0) continue;
        const char* col = kVehicleColors[color_idx % kNumColors];
        const auto& veh = inst.vehicles[r.vehicle_id];
        double load = 0.0;
        for (int c : r.customers) load += inst.demand[c];

        out << "<rect x=\"" << sb_x << "\" y=\"" << (row_y - 10)
            << "\" width=\"14\" height=\"14\" fill=\"" << col << "\"/>\n";

        std::ostringstream info;
        info << "V" << (r.vehicle_id + 1)
             << "  Q=" << veh.capacity
             << "  load=" << load
             << "  F=" << veh.fixed_cost
             << "  V=" << veh.variable_cost;
        out << "<text x=\"" << (sb_x + 22) << "\" y=\"" << row_y
            << "\">" << xml_escape(info.str()) << "</text>\n";
        row_y += 18;

        std::ostringstream seq;
        seq << "0→";
        for (int c : r.customers) seq << c << "→";
        seq << "0";
        out << "<text x=\"" << (sb_x + 22) << "\" y=\"" << row_y
            << "\" fill=\"#555\">" << xml_escape(seq.str()) << "</text>\n";
        row_y += 22;
        ++color_idx;
    }

    // Legenda de prioridade.
    if (max_p > 0.0) {
        row_y += 6;
        out << "<text x=\"" << sb_x << "\" y=\"" << row_y
            << "\" font-weight=\"600\">Priority (P)</text>\n";
        row_y += 18;
        const double steps[] = {0.0, 0.25, 0.5, 1.0};
        const char* labels[] = {"0", "low", "mid", "high"};
        for (int i = 0; i < 4; ++i) {
            out << "<circle cx=\"" << (sb_x + 8) << "\" cy=\"" << row_y
                << "\" r=\"6\" fill=\"" << priority_fill(steps[i] * max_p, max_p)
                << "\" stroke=\"#333\"/>\n";
            out << "<text x=\"" << (sb_x + 24) << "\" y=\"" << (row_y + 4)
                << "\">" << labels[i] << "</text>\n";
            row_y += 20;
        }
    }

    // Rodape: decomposicao do custo e legenda.
    std::ostringstream footer;
    footer << "cost_operational = " << sol.cost_operational
           << "   |   cost_priority = " << sol.cost_priority
           << "   |   cost_total = " << sol.cost_total;
    out << "<text x=\"" << MARGIN << "\" y=\"" << (H - FOOTER + 30)
        << "\" font-size=\"14\" font-weight=\"500\">"
        << xml_escape(footer.str()) << "</text>\n";

    if (extras.exact && sol.feasible && !extras.optimal) {
        std::ostringstream f2;
        f2 << "Solver stopped before proving optimality — LB = "
           << fmt_num(extras.lower_bound, 2)
           << ", gap = " << fmt_num(100.0 * extras.gap, 2) << " %.";
        out << "<text x=\"" << MARGIN << "\" y=\"" << (H - FOOTER + 54)
            << "\" fill=\"#b22222\">" << xml_escape(f2.str()) << "</text>\n";
    }

    std::ostringstream caption;
    caption << "Circle size ∝ demand.  Circle colour ∝ priority "
            << "(gray = 0, red = max).  Dashed arc returns to depot.";
    out << "<text x=\"" << MARGIN << "\" y=\"" << (H - FOOTER + 78)
        << "\" fill=\"#555\">" << xml_escape(caption.str()) << "</text>\n";

    out << "</svg>\n";
}

} // namespace hfvrp
