#include <iostream>
#include <iomanip>
#include <vector>
#include <string>


// Include FreeType headers
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

// --- Data Structures for Storing Glyph Vector Data ---

enum class PointType {
    MOVE_TO,
    LINE_TO,
    CONIC_TO // Quadratic Bezier
};

struct VectorPoint {
    long x;
    long y;
};

struct PathSegment {
    PointType type;
    VectorPoint to;
    VectorPoint control; // Only used for CONIC_TO
};

using Contour = std::vector<PathSegment>;
using GlyphPath = std::vector<Contour>;

// --- FreeType Decomposition Callback Functions ---

// Callback: A new contour begins with a "move to" command.
int MoveToFunc( const FT_Vector* to, void* user ) {
    GlyphPath* path = static_cast<GlyphPath*>( user );
    // Start a new contour
    path->push_back( {} );
    // Add the MOVE_TO segment
    path->back().push_back( { PointType::MOVE_TO, {to->x, to->y}, {0, 0} } );
    return 0;
}

// Callback: A line segment is defined by a "line to" command.
int LineToFunc( const FT_Vector* to, void* user ) {
    GlyphPath* path = static_cast<GlyphPath*>( user );
    path->back().push_back( { PointType::LINE_TO, {to->x, to->y}, {0, 0} } );
    return 0;
}

// Callback: A quadratic Bezier curve is defined by a control point and an end point.
int ConicToFunc( const FT_Vector* control, const FT_Vector* to, void* user ) {
    GlyphPath* path = static_cast<GlyphPath*>( user );
    path->back().push_back( { PointType::CONIC_TO, {to->x, to->y}, {control->x, control->y} } );
    return 0;
}

// Not used for TrueType fonts, which use quadratic splines.
int CubicToFunc( const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user ) {
    return 0;
}


// --- Main Application Logic ---

void PrintGlyphPath( const GlyphPath& path ) {
    std::cout << "// Extracted Glyph Path:\n";
    for ( size_t i = 0; i < path.size(); ++i ) {
        std::cout << "   Contour #" << std::setw(2) << (i + 1) << "\n";
        for ( const auto& segment : path[i] ) {
            switch ( segment.type ) {
                case PointType::MOVE_TO:
                    std::cout << "      MoveTo ("
                        << std::right << std::setw(5) << segment.to.x << ", "
                        << std::right << std::setw(5) << segment.to.y << ")\n";
                    break;
                case PointType::LINE_TO:
                    std::cout << "      LineTo ("
                        << std::right << std::setw(5) << segment.to.x << ", "
                        << std::right << std::setw(5) << segment.to.y << ")\n";
                    break;
                case PointType::CONIC_TO:
                    std::cout << "      QuadTo ("
                        << std::right << std::setw(5) << segment.control.x << ", "
                        << std::right << std::setw(5) << segment.control.y << ") ("
                        << std::right << std::setw(5) << segment.to.x << ", "
                        << std::right << std::setw(5) << segment.to.y << ")\n";
                    break;
            }
        }
    }
}

int main( int argc, char** argv ) {
    if ( argc != 3 ) {
        std::cerr << "Usage: " << argv[0] << " <font_path.ttf> <character>\n";
        return 1;
    }

    const char* font_path = argv[1];
    char character_to_load = argv[2][0];

    FT_Library library;
    FT_Error error = FT_Init_FreeType( &library );
    if ( error ) {
        std::cerr << "Error: Could not initialize FreeType library. Code: " << error << std::endl;
        return 1;
    }

    FT_Face face;
    error = FT_New_Face( library, font_path, 0, &face );
    if ( error == FT_Err_Unknown_File_Format ) {
        std::cerr << "Error: The font file could be opened but its format is unsupported." << std::endl;
        FT_Done_FreeType( library );
        return 1;
    } else if ( error ) {
        std::cerr << "Error: Could not open or process font file: " << font_path << std::endl;
        FT_Done_FreeType( library );
        return 1;
    }

    // Get the glyph index from the character code
    FT_UInt glyph_index = FT_Get_Char_Index( face, static_cast<FT_ULong>( character_to_load ) );
    if ( glyph_index == 0 ) {
        std::cerr << "Error: Glyph not found for character '" << character_to_load << "'." << std::endl;
        FT_Done_Face( face );
        FT_Done_FreeType( library );
        return 1;
    }

    // Load the glyph. The key is the FT_LOAD_NO_SCALE flag.
    // This flag ensures that the outline coordinates are returned in their original
    // "font units" rather than being scaled to a pixel size.
    // FT_LOAD_NO_HINTING disables the hinter, which we are ignoring.
    error = FT_Load_Glyph( face, glyph_index, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING );
    if ( error ) {
        std::cerr << "Error: Could not load glyph. Code: " << error << std::endl;
        FT_Done_Face( face );
        FT_Done_FreeType( library );
        return 1;
    }

    if ( face->glyph->format != FT_GLYPH_FORMAT_OUTLINE ) {
        std::cerr << "Error: Glyph format is not an outline." << std::endl;
        FT_Done_Face( face );
        FT_Done_FreeType( library );
        return 1;
    }

    // Set up the function pointers for decomposition
    FT_Outline_Funcs callbacks;
    callbacks.move_to = MoveToFunc;
    callbacks.line_to = LineToFunc;
    callbacks.conic_to = ConicToFunc;
    callbacks.cubic_to = CubicToFunc;
    callbacks.shift = 0;
    callbacks.delta = 0;

    // Create a GlyphPath object to be populated by the callbacks
    GlyphPath glyph_path;

    // Decompose the outline, passing our path object through the `user` pointer
    error = FT_Outline_Decompose( &face->glyph->outline, &callbacks, &glyph_path );
    if ( error ) {
        std::cerr << "Error: Could not decompose glyph outline. Code: " << error << std::endl;
        FT_Done_Face( face );
        FT_Done_FreeType( library );
        return 1;
    }

    std::cout << "// Successfully extracted vector data for character '" << character_to_load << "' from " << font_path << ".\n";
    PrintGlyphPath( glyph_path );

    // Cleanup
    FT_Done_Face( face );
    FT_Done_FreeType( library );

    return 0;
}
