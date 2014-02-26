#include "geopal_parser.h"

namespace ed { namespace connectors {

GeopalParser::GeopalParser(const std::string& path, const ed::connectors::ConvCoord& conv_coord): path(path), conv_coord(conv_coord){
    logger = log4cplus::Logger::getInstance("log");
    try{
            boost::filesystem::path directory(this->path);
            boost::filesystem::directory_iterator iter(directory), end;
             for(;iter != end; ++iter){
                 if (iter->path().extension() == ".txt"){
                    std::string file_name = iter->path().filename().string();
                    this->files.push_back(file_name);
                 }
             }
        }catch( const boost::filesystem::filesystem_error& e){
            throw GeopalParserException("GeopalParser : Error " + e.code().message());
        }
}

bool GeopalParser::starts_with(std::string filename, const std::string& prefex){
    return boost::algorithm::starts_with(filename, prefex);
}

void GeopalParser::fill(ed::Georef& data){
//    chargement des données
    this->fill_admins(data);
    LOG4CPLUS_INFO(logger, "Admin count :" << data.admins.size());
    this->fill_nodes(data);
    LOG4CPLUS_INFO(logger, "Noeud count :" << data.nodes.size());
    this->fill_ways_edges(data);
    LOG4CPLUS_INFO(logger, "Way count :" << data.ways.size());
    LOG4CPLUS_INFO(logger, "Edge count :" << data.edges.size());
    this->fill_house_numbers(data);
    LOG4CPLUS_INFO(logger, "House number count :" << data.house_numbers.size());
    this->fill_poi_types(data);
    LOG4CPLUS_INFO(logger, "PoiTypes count :" << data.poi_types.size());
    this->fill_pois(data);
    LOG4CPLUS_INFO(logger, "Pois count :" << data.pois.size());
}

ed::types::Node* GeopalParser::add_node(ed::Georef& data, const navitia::type::GeographicalCoord& coord, const std::string& uri){
    ed::types::Node* node = new ed::types::Node;
    node->id = data.nodes.size() + 1;
    node->coord = this->conv_coord.convert_to(coord);
    data.nodes[uri] = node;
    return node;
}

void GeopalParser::fill_nodes(ed::Georef& data){
    for(const std::string file_name : this->files){
        if(!this->starts_with(file_name, "adresse")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"code_insee", "code_post", "x_adresse", "y_adresse"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file "
                                        + reader.filename + " . Not find column : "
                                        + reader.missing_headers(mandatory_headers));
        }
        int insee_c = reader.get_pos_col("code_insee");
        int code_post_c = reader.get_pos_col("code_post");
        int x_c = reader.get_pos_col("x_adresse");
        int y_c = reader.get_pos_col("y_adresse");
        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            std::string uri;
            if (reader.is_valid(x_c, row) && reader.is_valid(y_c, row) && reader.is_valid(insee_c, row)){
                uri = row[x_c] + row[y_c];
                auto adm = data.admins.find(row[insee_c]);
                if (adm != data.admins.end()){
                    if(reader.is_valid(code_post_c, row)){
                        adm->second->postcode = row[code_post_c];
                    }
                    auto nd = data.nodes.find(uri);
                    if(nd == data.nodes.end())
                        this->add_node(data, navitia::type::GeographicalCoord(str_to_double(row[x_c]), str_to_double(row[y_c])), uri);
                }
            }
        }
    }
}

void GeopalParser::fill_admins(ed::Georef& data){
    for(const std::string file_name : this->files){
        if (! this->starts_with(file_name, "commune")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"nom" , "code_insee", "x_commune", "y_commune"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file " + reader.filename +" . Not find column : " + reader.missing_headers(mandatory_headers));
        }
        int name_c = reader.get_pos_col("nom");
        int insee_c = reader.get_pos_col("code_insee");
        int x_c = reader.get_pos_col("x_commune");
        int y_c = reader.get_pos_col("y_commune");
        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            if (reader.is_valid(insee_c, row) && reader.is_valid(x_c, row) && reader.is_valid(y_c, row)){
                auto itm = data.admins.find(row[insee_c]);
                if(itm == data.admins.end()){
                    ed::types::Admin* admin = new ed::types::Admin;
                    admin->insee = row[insee_c];
                    admin->id = data.admins.size() + 1;
                    if (reader.is_valid(name_c, row))
                        admin->name = row[name_c];
                    admin->coord = this->conv_coord.convert_to(navitia::type::GeographicalCoord(str_to_double(row[x_c]), str_to_double(row[y_c])));
                    data.admins[admin->insee] = admin;
                }
            }
        }
    }
}

void GeopalParser::fill_ways_edges(ed::Georef& data){
    for(const std::string file_name : this->files){
        if (! this->starts_with(file_name, "route_a")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"id", "x_debut" , "y_debut", "x_fin", "y_fin", "longueur", "inseecom_g",
        "inseecom_d"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file " + reader.filename +" . Not find column : " + reader.missing_headers(mandatory_headers));
        }
        int nom_voie_d = reader.get_pos_col("nom_voie_d");
        int x1 = reader.get_pos_col("x_debut");
        int y1 = reader.get_pos_col("y_debut");
        int x2 = reader.get_pos_col("x_fin");
        int y2 = reader.get_pos_col("y_fin");
        int l = reader.get_pos_col("longueur");
        int inseecom_d = reader.get_pos_col("inseecom_d");
        int id = reader.get_pos_col("id");
        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            if (reader.is_valid(x1, row) && reader.is_valid(y1, row)
                && reader.is_valid(x2, row) && reader.is_valid(y2, row)
                && reader.is_valid(inseecom_d, row)
                && reader.is_valid(id, row)){
                auto admin = data.admins.find(row[inseecom_d]);
                if(admin != data.admins.end()){
                    std::string source  = row[x1] + row[y1];
                    std::string target  = row[x2] + row[y2];
                    std::string edge_uri = source + target;
                    ed::types::Node* source_node;
                    ed::types::Node* target_node;
                    auto source_it = data.nodes.find(source);
                    auto target_it = data.nodes.find(target);

                    if(source_it == data.nodes.end()){
                        source_node = this->add_node(data, navitia::type::GeographicalCoord(str_to_double(row[x1]), str_to_double(row[y1])), source);
                    }else{
                        source_node = source_it->second;
                    }

                    if(target_it == data.nodes.end()){
                        target_node = this->add_node(data, navitia::type::GeographicalCoord(str_to_double(row[x2]), str_to_double(row[y2])), target);
                    }else{
                        target_node = target_it->second;
                    }
                    ed::types::Way* current_way = nullptr;
                    std::string wayd_uri = row[id];
                    auto way = data.ways.find(wayd_uri);
                    if(way == data.ways.end()){
                        ed::types::Way* wy = new ed::types::Way;
                        wy->admin = admin->second;
                        admin->second->is_used = true;
                        wy->id = data.ways.size() + 1;
                        if(reader.is_valid(nom_voie_d, row)){
                            wy->name = row[nom_voie_d];
                        }
                        wy->type ="";
                        data.ways[wayd_uri] = wy;
                        current_way = wy;
                    }else{
                        current_way = way->second;
                    }
                    auto edge = data.edges.find(edge_uri);
                    if(edge == data.edges.end()){
                        ed::types::Edge* edg = new ed::types::Edge;
                        edg->source = source_node;
                        edg->source->is_used = true;
                        edg->target = target_node;
                        edg->target->is_used = true;
                        if(reader.is_valid(l, row)){
                            edg->length = str_to_int(row[l]);
                        }
                        edg->way = current_way;
                        data.edges[edge_uri]= edg;
                    }
                }
            }
        }
    }
}

void GeopalParser::fill_house_numbers(ed::Georef& data){
    for(const std::string file_name : this->files){
        if (! this->starts_with(file_name, "adresse")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"id_tr", "numero", "nom_voie", "code_insee", "x_adresse", "y_adresse"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file " + reader.filename +" . Not find column : " + reader.missing_headers(mandatory_headers));
        }
        int insee_c = reader.get_pos_col("code_insee");
        int nom_voie_c = reader.get_pos_col("nom_voie");
        int numero_c = reader.get_pos_col("numero");
        int x_c = reader.get_pos_col("x_adresse");
        int y_c = reader.get_pos_col("y_adresse");
        int id_tr = reader.get_pos_col("id_tr");
        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            if (reader.is_valid(x_c, row) && reader.is_valid(y_c, row)
                && reader.is_valid(insee_c, row) && reader.is_valid(numero_c, row)
                && reader.is_valid(nom_voie_c, row)
                && reader.is_valid(id_tr, row)){
                std::string way_uri = row[id_tr];
                auto way_it = data.ways.find(way_uri);
                if(way_it != data.ways.end()){
                    std::string hn_uri = row[x_c] + row[y_c] + row[numero_c];
                    auto hn = data.house_numbers.find(hn_uri);
                    if (hn == data.house_numbers.end()){
                        ed::types::HouseNumber* current_hn = new ed::types::HouseNumber;
                        current_hn->coord = this->conv_coord.convert_to(navitia::type::GeographicalCoord(str_to_double(row[x_c]), str_to_double(row[y_c])));
                        current_hn->number = row[numero_c];
                        current_hn->way = way_it->second;
                        data.house_numbers[hn_uri] = current_hn;
                    }
                }
            }
        }
    }
}

void GeopalParser::fill_poi_types(ed::Georef& data){
    for(const std::string file_name : this->files){
        if (! this->starts_with(file_name, "poi_type")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"poi_type_id" , "poi_type_name"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file " + reader.filename +" . Not find column : " + reader.missing_headers(mandatory_headers));
        }
        int id_c = reader.get_pos_col("poi_type_id");
        int name_c = reader.get_pos_col("poi_type_name");
        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            if (reader.is_valid(id_c, row) && reader.is_valid(name_c, row)){
                const auto& itm = data.poi_types.find(row[id_c]);
                if(itm == data.poi_types.end()){
                    ed::types::PoiType* poi_type = new ed::types::PoiType;
                    poi_type->id = data.poi_types.size() + 1;
                    poi_type->name = row[name_c];
                    data.poi_types[row[id_c]] = poi_type;
                }
            }
        }
    }
}
void GeopalParser::fill_pois(ed::Georef& data){
    for(const std::string file_name : this->files){
        if (! this->starts_with(file_name, "poi.txt")){
            continue;
        }
        CsvReader reader(this->path + "/" + file_name, ';', true, true);
        if(!reader.is_open()) {
            throw GeopalParserException("Error on open file " + reader.filename);
        }
        std::vector<std::string> mandatory_headers = {"poi_id", "poi_name", "poi_weight", "poi_visible", "poi_lat", "poi_lon", "poi_type_id"};
        if(!reader.validate(mandatory_headers)) {
            throw GeopalParserException("Impossible to parse file " + reader.filename +" . Not find column : " + reader.missing_headers(mandatory_headers));
        }
        int id_c = reader.get_pos_col("poi_id");
        int name_c = reader.get_pos_col("poi_name");
        int weight_c = reader.get_pos_col("poi_weight");
        int visible_c = reader.get_pos_col("poi_visible");
        int lat_c = reader.get_pos_col("poi_lat");
        int lon_c = reader.get_pos_col("poi_lon");
        int type_id_c = reader.get_pos_col("poi_type_id");

        while(!reader.eof()){
            std::vector<std::string> row = reader.next();
            if (reader.is_valid(id_c, row) && reader.is_valid(name_c, row)
                && reader.is_valid(weight_c, row) && reader.is_valid(visible_c, row)
                && reader.is_valid(lat_c, row) && reader.is_valid(lon_c, row)
                && reader.is_valid(type_id_c, row)){
                const auto& itm = data.pois.find(row[id_c]);
                if(itm == data.pois.end()){
                    const auto& poi_type = data.poi_types.find(row[type_id_c]);
                    if(poi_type != data.poi_types.end()){
                        ed::types::Poi* poi = new ed::types::Poi;
                        poi->id = data.pois.size() + 1;
                        poi->name = row[name_c];
                        try{
                            poi->visible = boost::lexical_cast<bool>(row[visible_c]);
                        }catch(boost::bad_lexical_cast ) {
                            LOG4CPLUS_WARN(logger, "Impossible to parse the visible for " + row[id_c] + " " + row[name_c]);
                            poi->visible = true;
                        }
                        try{
                            poi->weight = boost::lexical_cast<int>(row[weight_c]);
                        }catch(boost::bad_lexical_cast ) {
                            LOG4CPLUS_WARN(logger, "Impossible to parse the weight for " + row[id_c] + " " + row[name_c]);
                            poi->weight = 0;
                        }
                        poi->poi_type = poi_type->second;
                        poi->coord = this->conv_coord.convert_to(navitia::type::GeographicalCoord(str_to_double(row[lon_c]), str_to_double(row[lat_c])));
                        data.pois[row[id_c]] = poi;
                    }
                }
            }
        }
    }
}
}
}//namespace