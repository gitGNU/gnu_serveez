;; -*-scheme-*-
;;
;; serveez.scm - convenience functions
;;
;; Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>
;; Copyright (C) 2001 Martin Grabmueller <mgrabmue@cs.tu-berlin.de>
;;
;; This is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;; 
;; This software is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.
;; 
;; You should have received a copy of the GNU General Public License
;; along with this package; see the file COPYING.  If not, write to
;; the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
;; Boston, MA 02111-1307, USA.
;;
;; $Id: serveez.scm,v 1.1 2001/06/13 21:20:55 ela Exp $
;;

;;
;; === Miscellaneous functions - Scheme for beginners, thanks to 'mgrabmue.
;;
(define (println . args)
  (for-each display args) (newline))
(define (printsln spacer . args)
  (for-each (lambda (x) (display x) (display spacer)) args) (newline))

;;
;; === Network interfaces
;;
(define (interface-add! . ifc)
  (serveez-interfaces (append! (serveez-interfaces) ifc)))

;;
;; === Additional search paths for server modules
;;
(define (loadpath-add! . path)
  (serveez-loadpath (append! (serveez-loadpath) path)))

;;
;; === TODO: Enhanced server bindings
;;
(define (bind-servers! . args)
  ())