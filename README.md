# pdftoopvp

[![CircleCI](https://circleci.com/gh/mpsuzuki/pdftoopvp/tree/master.svg?style=svg)](https://circleci.com/gh/mpsuzuki/pdftoopvp/tree/master)

pdftoopvp is a poppler-based cups-filter, which renders a PDF onto OPVP
(OpenPrinting Vector Protocol) device. This was incorporated on 2012,
but frequent change of Poppler's non-public API and little feedback from
users, cups-filter development team decided to disable by default on 2017,
and finally drop from official tree (as pdftoijs).
