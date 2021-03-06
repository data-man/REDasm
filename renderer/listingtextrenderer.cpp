#include "listingtextrenderer.h"
#include "../themeprovider.h"
#include <cmath>
#include <QApplication>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QPalette>
#include <QPainter>

ListingTextRenderer::ListingTextRenderer(REDasm::DisassemblerAPI *disassembler): ListingRendererCommon(disassembler) { }

void ListingTextRenderer::renderLine(const REDasm::RendererLine &rl)
{
    if(rl.index > 0)
        m_maxwidth = std::max(m_maxwidth, m_fontmetrics.boundingRect(QString::fromStdString(rl.text)).width());
    else
        m_maxwidth = m_fontmetrics.boundingRect(QString::fromStdString(rl.text)).width();

    int y = (rl.documentindex - m_firstline) * m_fontmetrics.height();
    ListingRendererCommon::renderText(rl, 0, y, m_fontmetrics);
}
