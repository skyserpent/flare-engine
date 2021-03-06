/*
Copyright © 2011-2012 Clint Bellanger and Thane Brimhall
Copyright © 2013 Kurt Rinnert
Copyright © 2014 Henrik Andersson

This file is part of FLARE.

FLARE is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

FLARE is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
FLARE.  If not, see http://www.gnu.org/licenses/
*/

/*
 * class FontEngine
 */

#include "CommonIncludes.h"
#include "FontEngine.h"
#include "FileParser.h"
#include "SharedResources.h"
#include "Settings.h"
#include "UtilsParsing.h"

FontStyle::FontStyle() : name(""), path(""), ptsize(0), blend(true), ttfont(NULL), line_height(0), font_height(0) {
}

FontEngine::FontEngine()
	: active_font(NULL)
	, cursor_y(0) {
	// Initiate SDL_ttf
	if(!TTF_WasInit() && TTF_Init()==-1) {
		logError("FontEngine: TTF_Init: %s\n", TTF_GetError());
		exit(2);
	}

	// load the fonts
	// @CLASS FontEngine: Font settings|Description of engine/font_settings.txt
	FileParser infile;
	if (infile.open("engine/font_settings.txt")) {
		while (infile.next()) {
			if (infile.new_section) {
				FontStyle f;
				f.name = infile.section;
				font_styles.push_back(f);
			}

			if (font_styles.empty()) continue;

			FontStyle *style = &(font_styles.back());
			if ((infile.key == "default" && style->path == "") || infile.key == LANGUAGE) {
				// @ATTR $STYLE.default, $STYLE.$LANGUAGE|filename (string), point size (integer), blending (boolean)|Filename, point size, and blend mode of the font to use for this language. $STYLE can be something like "font_normal" or "font_bold". $LANGUAGE can be a 2-letter region code.
				style->path = popFirstString(infile.val);
				style->ptsize = popFirstInt(infile.val);
				style->blend = toBool(popFirstString(infile.val));
				style->ttfont = TTF_OpenFont(mods->locate("fonts/" + style->path).c_str(), style->ptsize);
				if(style->ttfont == NULL) {
					logError("FontEngine: TTF_OpenFont: %s\n", TTF_GetError());
				}
				else {
					int lineskip = TTF_FontLineSkip(style->ttfont);
					style->line_height = lineskip;
					style->font_height = lineskip;
				}
			}
		}
		infile.close();
	}

	// set the font colors
	Color color;
	if (infile.open("engine/font_colors.txt")) {
		while (infile.next()) {
			// @ATTR menu_normal, menu_bonus, menu_penalty, widget_normal, widget_disabled|r (integer), g (integer), b (integer)|Colors for menus and widgets
			// @ATTR combat_givedmg, combat_takedmg, combat_crit, combat_buff, combat_miss|r (integer), g (integer), b (integer)|Colors for combat text
			// @ATTR requirements_not_met, item_bonus, item_penalty, item_flavor|r (integer), g (integer), b (integer)|Colors for tooltips
			// @ATTR item_$QUALITY|r (integer), g (integer), b (integer)|Colors for item quality. $QUALITY should match qualities used in items/items.txt
			color_map[infile.key] = toRGB(infile.val);
		}
		infile.close();
	}

	// Attempt to set the default active font
	setFont("font_regular");
	if (!active_font) {
		logError("FontEngine: Unable to determine default font!\n");
		SDL_Quit();
		exit(1);
	}
}

Color FontEngine::getColor(std::string _color) {
	std::map<std::string,Color>::iterator it,end;
	for (it=color_map.begin(), end=color_map.end(); it!=end; ++it) {
		if (_color.compare(it->first) == 0) return it->second;
	}
	// If all else fails, return white;
	return FONT_WHITE;
}

void FontEngine::setFont(std::string _font) {
	for (unsigned int i=0; i<font_styles.size(); i++) {
		if (font_styles[i].name == _font) {
			active_font = &(font_styles[i]);
			return;
		}
	}
}

/**
 * For single-line text, just calculate the width
 */
int FontEngine::calc_width(const std::string& text) {
	int w, h;
	TTF_SizeUTF8(active_font->ttfont, text.c_str(), &w, &h);
	return w;
}

/**
 * Using the given wrap width, calculate the width and height necessary to display this text
 */
Point FontEngine::calc_size(const std::string& text_with_newlines, int width) {
	char newline = 10;

	std::string text = text_with_newlines;

	// if this contains newlines, recurse
	size_t check_newline = text.find_first_of(newline);
	if (check_newline != std::string::npos) {
		Point p1 = calc_size(text.substr(0, check_newline), width);
		Point p2 = calc_size(text.substr(check_newline+1, text.length()), width);
		Point p3;

		if (p1.x > p2.x) p3.x = p1.x;
		else p3.x = p2.x;

		p3.y = p1.y + p2.y;
		return p3;
	}

	int height = 0;
	int max_width = 0;

	std::string next_word;
	std::stringstream builder;
	std::stringstream builder_prev;
	char space = 32;
	size_t cursor = 0;
	std::string fulltext = text + " ";

	builder.str("");
	builder_prev.str("");

	next_word = getNextToken(fulltext, cursor, space);

	while(cursor != std::string::npos) {
		builder << next_word;

		if (calc_width(builder.str()) > width) {

			// this word can't fit on this line, so word wrap
			height = height + getLineHeight();
			if (calc_width(builder_prev.str()) > max_width) {
				max_width = calc_width(builder_prev.str());
			}

			builder_prev.str("");
			builder.str("");

			builder << next_word << " ";
		}
		else {
			builder <<  " ";
			builder_prev.str(builder.str());
		}

		next_word = getNextToken(fulltext, cursor, space); // get next word
	}

	height = height + getLineHeight();
	builder.str(trim(builder.str())); //removes whitespace that shouldn't be included in the size
	if (calc_width(builder.str()) > max_width) max_width = calc_width(builder.str());

	Point size;
	size.x = max_width;
	size.y = height;
	return size;
}


/**
 * Render the given text at (x,y) on the target image.
 * Justify is left, right, or center
 */
void FontEngine::render(const std::string& text, int x, int y, int justify, Image *target, Color color) {
	Rect clip, dest_rect;
	Image *graphics;
	Sprite *temp;

	// calculate actual starting x,y based on justify
	if (justify == JUSTIFY_LEFT) {
		dest_rect.x = x;
		dest_rect.y = y;
	}
	else if (justify == JUSTIFY_RIGHT) {
		dest_rect.x = x - calc_width(text);
		dest_rect.y = y;
	}
	else if (justify == JUSTIFY_CENTER) {
		dest_rect.x = x - calc_width(text)/2;
		dest_rect.y = y;
	}
	else {
		logError("FontEngine::render() given unhandled 'justify=%d', assuming left\n",justify);
		dest_rect.x = x;
		dest_rect.y = y;
	}

	// Render text directly onto screen
	if (!target) {
		render_device->renderText(active_font->ttfont, text, color, dest_rect);
		return;
	}

	// Render text into target Image
	graphics = render_device->renderTextToImage(active_font->ttfont, text, color, active_font->blend);
	if (!graphics) return;
	temp = graphics->createSprite();
	graphics->unref();

	// Render text graphics into target
	clip = temp->getClip();
	render_device->renderToImage(temp->getGraphics(), clip,
								 target, dest_rect, active_font->blend);

	// text is cached, we can free temp resource
	delete temp;
}

/**
 * Word wrap to width
 */
void FontEngine::render(const std::string& text, int x, int y, int justify, Image *target, int width, Color color) {

	std::string fulltext = text + " ";
	cursor_y = y;
	std::string next_word;
	std::stringstream builder;
	std::stringstream builder_prev;
	char space = 32;
	size_t cursor = 0;

	builder.str("");
	builder_prev.str("");

	next_word = getNextToken(fulltext, cursor, space);

	while(cursor != std::string::npos) {

		builder << next_word;

		if (calc_width(builder.str()) > width) {
			render(builder_prev.str(), x, cursor_y, justify, target, color);
			cursor_y += getLineHeight();
			builder_prev.str("");
			builder.str("");

			builder << next_word << " ";
		}
		else {
			builder << " ";
			builder_prev.str(builder.str());
		}

		next_word = getNextToken(fulltext, cursor, space); // next word
	}

	render(builder.str(), x, cursor_y, justify, target, color);
	cursor_y += getLineHeight();

}

void FontEngine::renderShadowed(const std::string& text, int x, int y, int justify, Image *target, Color color) {
	render(text, x+1, y+1, justify, target, FONT_BLACK);
	render(text, x, y, justify, target, color);
}

void FontEngine::renderShadowed(const std::string& text, int x, int y, int justify, Image *target, int width, Color color) {
	render(text, x+1, y+1, justify, target, width, FONT_BLACK);
	render(text, x, y, justify, target, width, color);
}

FontEngine::~FontEngine() {
	for (unsigned int i=0; i<font_styles.size(); ++i) TTF_CloseFont(font_styles[i].ttfont);
	TTF_Quit();
}

