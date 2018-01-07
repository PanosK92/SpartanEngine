/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ================
#include <QComboBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
//===========================

namespace Directus
{
    class Context;
    class Renderer;
};

class DirectusRenderFlags : public QComboBox
{
    Q_OBJECT
public:
    DirectusRenderFlags(QWidget* parent = 0);
    ~DirectusRenderFlags();
    void Initialize(Directus::Context* context);
    virtual void showPopup();

private slots:
    void OnItemPressed(const QModelIndex& index);
    void OnCheckBoxPressed();

private:
    void AddCheckItem(int row, const QString& text);
    void MapRenderFlags();
    void ReflectRenderFlags();

    QStandardItemModel* m_model;
    Directus::Context* m_context;
    Directus::Renderer* m_renderer;
    int m_rows;
    QString m_defaultText;

public:
    class QCheckListStyledItemDelegate : public QStyledItemDelegate
    {
        public:
        QCheckListStyledItemDelegate(QObject* parent = 0) : QStyledItemDelegate(parent) {}

        void paint(QPainter * painter_, const QStyleOptionViewItem & option_, const QModelIndex & index_) const
        {
            // Remove border from items
            QStyleOptionViewItem & refToNonConstOption = const_cast<QStyleOptionViewItem &>(option_);
            refToNonConstOption.showDecorationSelected = false;
            refToNonConstOption.state &= ~QStyle::State_HasFocus & ~QStyle::State_MouseOver;
            QStyledItemDelegate::paint(painter_, refToNonConstOption, index_);
        }
    };
};
