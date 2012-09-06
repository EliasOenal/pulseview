/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "sigviewport.h"

#include "sigsession.h"
#include "signal.h"

#include "extdef.h"

#include <QMouseEvent>
#include <QTextStream>

#include <math.h>

#include <boost/foreach.hpp>

using namespace boost;
using namespace std;

const double SigViewport::MaxScale = 1e9;
const double SigViewport::MinScale = 1e-15;

const int SigViewport::SignalHeight = 50;
const int SigViewport::LabelMarginWidth = 70;
const int SigViewport::RulerHeight = 30;

const int SigViewport::MinorTickSubdivision = 4;
const int SigViewport::ScaleUnits[3] = {1, 2, 5};

const QString SigViewport::SIPrefixes[9] =
	{"f", "p", "n", QChar(0x03BC), "m", "", "k", "M", "G"};
const int SigViewport::FirstSIPrefixPower = -15;

SigViewport::SigViewport(SigSession &session, QWidget *parent) :
	QGLWidget(parent),
        _session(session),
	_scale(1e-6),
	_offset(0)
{
	connect(&_session, SIGNAL(data_updated()),
		this, SLOT(data_updated()));

	setMouseTracking(true);
	setAutoFillBackground(false);
}

void SigViewport::zoom(double steps)
{
	zoom(steps, (width() - LabelMarginWidth) / 2);
}

void SigViewport::initializeGL()
{
}

void SigViewport::resizeGL(int width, int height)
{
	setup_viewport(width, height);
}

void SigViewport::paintEvent(QPaintEvent *event)
{
	int offset;

	const vector< shared_ptr<Signal> > &sigs =
		_session.get_signals();

	// Prepare for OpenGL rendering
	makeCurrent();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	setup_viewport(width(), height());

	qglClearColor(Qt::white);
	glClear(GL_COLOR_BUFFER_BIT);

	// Plot the signal
	glEnable(GL_SCISSOR_TEST);
	glScissor(LabelMarginWidth, 0, width(), height());
	offset = RulerHeight;
	BOOST_FOREACH(const shared_ptr<Signal> s, sigs)
	{
		assert(s);

		const QRect signal_rect(LabelMarginWidth, offset,
			width() - LabelMarginWidth, SignalHeight);

		s->paint(*this, signal_rect, _scale, _offset);

		offset += SignalHeight;
	}

	glDisable(GL_SCISSOR_TEST);

	// Prepare for QPainter rendering
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Paint the labels
	offset = RulerHeight;
	BOOST_FOREACH(const shared_ptr<Signal> s, sigs)
	{
		assert(s);

		const QRect label_rect(0, offset,
			LabelMarginWidth, SignalHeight);
		s->paint_label(painter, label_rect);

		offset += SignalHeight;
	}

	// Paint the ruler
	paint_ruler(painter);

	painter.end();
}

void SigViewport::data_updated()
{
	update();
}

void SigViewport::mousePressEvent(QMouseEvent *event)
{
	assert(event);

	_mouse_down_point = event->pos();
	_mouse_down_offset = _offset;
}

void SigViewport::mouseMoveEvent(QMouseEvent *event)
{
	assert(event);

	if(event->buttons() & Qt::LeftButton)
	{
		_offset = _mouse_down_offset + (_mouse_down_point - event->pos()).x() * _scale;
		update();
	}
}

void SigViewport::mouseReleaseEvent(QMouseEvent *event)
{
	assert(event);
}

void SigViewport::wheelEvent(QWheelEvent *event)
{
	assert(event);
	zoom(event->delta() / 120, event->x() - LabelMarginWidth);
}

void SigViewport::setup_viewport(int width, int height)
{
	glViewport(0, 0, (GLint)width, (GLint)height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, height, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
}

void SigViewport::paint_ruler(QPainter &p)
{
	const double MinSpacing = 80;

	const double min_period = _scale * MinSpacing;

	const int order = (int)floorf(log10f(min_period));
	const double order_decimal = pow(10, order);

	int unit = 0;
	double tick_period = 0.0f;

	do
	{
		tick_period = order_decimal * ScaleUnits[unit++];
	} while(tick_period < min_period && unit < countof(ScaleUnits));

	const int prefix = (order - FirstSIPrefixPower) / 3;
	assert(prefix >= 0);
	assert(prefix < countof(SIPrefixes));

	const int text_height = p.boundingRect(0, 0, INT_MAX, INT_MAX,
		Qt::AlignLeft | Qt::AlignTop, "8").height();

	// Draw the tick marks
	p.setPen(Qt::black);

	const double minor_tick_period = tick_period / MinorTickSubdivision;
	const double first_major_division = floor(_offset / tick_period);
	const double first_minor_division = ceil(_offset / minor_tick_period);
	const double t0 = first_major_division * tick_period;

	int division = (int)round(first_minor_division -
		first_major_division * MinorTickSubdivision);
	while(1)
	{
		const double t = t0 + division * minor_tick_period;
		const double x = (t - _offset) / _scale + LabelMarginWidth;

		if(x >= width())
			break;

		if(division % MinorTickSubdivision == 0)
		{
			// Draw a major tick
			QString s;
			QTextStream ts(&s);
			ts << (t / order_decimal) << SIPrefixes[prefix] << "s";
			p.drawText(x, 0, 0, text_height, Qt::AlignCenter | Qt::AlignTop |
				Qt::TextDontClip, s);
			p.drawLine(x, text_height, x, RulerHeight);
		}
		else
		{
			// Draw a minor tick
			p.drawLine(x, (text_height + RulerHeight) / 2, x, RulerHeight);
		}

		division++;
	}
}

void SigViewport::zoom(double steps, int offset)
{
	const double cursor_offset = _offset + _scale * offset;
	_scale *= pow(3.0/2.0, -steps);
	_scale = max(min(_scale, MaxScale), MinScale);
	_offset = cursor_offset - _scale * offset;
	update();
}