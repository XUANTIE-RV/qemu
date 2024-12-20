
# -*- coding: utf-8 -*-
#
# Configuration file for the Sphinx documentation builder.
#

# -- Project information

import sys
reload(sys)

project = u'玄铁QEMU用户手册'
author = u''
version = u''
release = u'V5.0'
copyright = 'copyright'

# -- General configuration

extensions = [
]
templates_path = ['_templates']
source_suffix = '.rst'
master_doc = 'index'
language = u'zh_CN'
exclude_patterns = []
pygments_style = 'sphinx'
numfig = True

# -- Options for HTML output

html_theme = 'alabaster'
html_static_path = ['_static']

# -- Options for HTMLHelp output

htmlhelp_basename = u'玄铁QEMU用户手册doc'

# -- Options for LaTeX output


latex_engine = 'xelatex'

latex_elements = {

    'preamble': r'''
\usepackage[UTF8, heading = true]{ctex}
\usepackage{ctex}
\setlength{\parindent}{2em}
\usepackage{enumitem}
\setlist[itemize,1]{leftmargin=1.2cm}
\setlist[itemize,2]{leftmargin=1.8cm}
\setlist[enumerate,1]{leftmargin=1.2cm}
\setlist[enumerate,2]{leftmargin=1.8cm}
\hypersetup{bookmarksnumbered = true}
\setcounter{secnumdepth}{3}
\usepackage{graphicx}
\usepackage{setspace}
\usepackage{subfigure}
\usepackage{float}
\usepackage{multirow}
\usepackage{fancyhdr}
\makeatletter
    \fancypagestyle{normal}{
        \fancyfoot[C]{\thepage}
        \fancyfoot[L]{\fontsize{7}{7} \selectfont \ www.xrvm.cn}
        \fancyfoot[R]{\fontsize{7}{7} \selectfont  \textcopyright\ 【杭州中天微系统有限公司】版权所有}
        \fancyhead[R]{\includegraphics[scale=0.45] {xuantielogo.png}}
        \fancyhead[L]{\py@HeaderFamily \nouppercase{\leftmark}}
     }
\makeatother
    ''',
    'classoptions': ',oneside',
    'maketitle': ur'''
\maketitle


\begin{spacing}{1.1}

\noindent \small \textbf{Copyright © 2023 Hangzhou C-SKY MicroSystems Co., Ltd. All rights reserved.}

\noindent This document is the property of Hangzhou C-SKY MicroSystems Co., Ltd. and its affiliates ("C-SKY"). This document may only be distributed to: (i) a C-SKY party having a legitimate business need for the information contained herein, or (ii) a non-C-SKY party having a legitimate business need for the information contained herein. No license, expressed or implied, under any patent, copyright or trade secret right is granted or implied by the conveyance of this document. No part of this document may be reproduced, transmitted, transcribed, stored in a retrieval system, translated into any language or computer language, in any form or by any means, electronic, mechanical, magnetic, optical, chemical, manual, or otherwise without the prior written permission of Hangzhou C-SKY MicroSystems Co., Ltd.

\noindent \small \textbf{Trademarks and Permissions}

\noindent The C-SKY Logo and all other trademarks indicated as such herein (including XuanTie) are trademarks of Hangzhou C-SKY MicroSystems Co., Ltd. All other products or service names are the property of their respective owners.

\noindent \small \textbf{Notice}

\noindent The purchased products, services and features are stipulated by the contract made between C-SKY and the customer. All or part of the products, services and features described in this document may not be within the purchase scope or the usage scope. Unless otherwise specified in the contract, all statements, information, and recommendations in this document are provided "AS IS" without warranties, guarantees or representations of any kind, either express or implied.

\noindent The information in this document is subject to change without notice. Every effort has been made in the preparation of this document to ensure accuracy of the contents, but all statements, information, and recommendations in this document do not constitute a warranty of any kind, express or implied.

\noindent \small \textbf{杭州中天微系统有限公司 Hangzhou C-SKY MicroSystems Co., LTD}

\noindent Address: Room 201, 2/F, Building 5, No.699 Wangshang Road , Hangzhou, Zhejiang, China

\noindent Website: www.xrvm.cn

\noindent \small \textbf{Copyright © 2023杭州中天微系统有限公司，保留所有权利.}

\noindent 本文档的所有权及知识产权归属于杭州中天微系统有限公司及其关联公司(下称“中天微”)。本文档仅能分派给：(i)拥有合法雇佣关系，并需要本文档的信息的中天微员工，或(ii)非中天微组织但拥有合法合作关系，并且其需要本文档的信息的合作方。对于本文档，未经杭州中天微系统有限公司明示同意，则不能使用该文档。在未经中天微的书面许可的情形下，不得复制本文档的任何部分，传播、转录、储存在检索系统中或翻译成任何语言或计算机语言。

\noindent \small \textbf{商标申明}

\noindent 中天微的LOGO和其它所有商标（如XuanTie玄铁）归杭州中天微系统有限公司及其关联公司所有，未经杭州中天微系统有限公司的书面同意，任何法律实体不得使用中天微的商标或者商业标识。

\noindent \small \textbf{注意}

\noindent 您购买的产品、服务或特性等应受中天微商业合同和条款的约束，本文档中描述的全部或部分产品、服务或特性可能不在您的购买或使用范围之内。除非合同另有约定，中天微对本文档内容不做任何明示或默示的声明或保证。

\noindent 由于产品版本升级或其他原因，本文档内容会不定期进行更新。除非另有约定，本文档仅作为使用指导，本文档中的所有陈述、信息和建议不构成任何明示或暗示的担保。杭州中天微系统有限公司不对任何第三方使用本文档产生的损失承担任何法律责任。

\noindent \small \textbf{杭州中天微系统有限公司 Hangzhou C-SKY MicroSystems Co., LTD}

\noindent 地址：中国浙江省杭州市网商路699号5号楼2楼201室

\noindent 网址：www.xrvm.cn

\end{spacing}

\newpage

\textbf{\huge 版本历史}\\

\begin{center}

\begin{tabular}{p{40pt}<{}p{300pt}<{}p{100pt}<{}}
\hline

\textbf{版本}       & \textbf{描述}               & \textbf{日期}                 \\ \hline
01                  & 第一次正式发布。              & 2021.07.31                  \\ \hline


\end{tabular}

\end{center}

	''',

}

latex_additional_files = ["_static/xuantielogo.png"]

latex_documents = [
    (master_doc, u'玄铁QEMU用户手册{}.tex'.format(release), u'玄铁QEMU用户手册',
     u'', 'manual'),
]

# -- Options for Texinfo output

texinfo_documents = [
    (master_doc, '玄铁QEMU用户手册', u'玄铁QEMU用户手册 Documentation',
     author, '玄铁QEMU用户手册', 'One line description of project.',
     'Miscellaneous'),
]
