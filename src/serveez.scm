;; -*-scheme-*-
;;
;; serveez.scm - convenience functions
;;
;; Copyright (C) 2001 Martin Grabmueller <mgrabmue@cs.tu-berlin.de>
;; Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>
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
;; $Id: serveez.scm,v 1.3 2001/06/21 11:25:47 ela Exp $
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
;; === Enhanced server bindings
;;
(define (bind-servers! . args)
  (let ((server-list '())  ;; Initialize lists.
        (port-list '()))

    ;; Iterate over argument list, separating ports from servers.
    (for-each
     (lambda (elem)
       (cond ((serveez-port? elem)
	      (set! port-list (cons elem port-list)))
	     ((serveez-server? elem)
	      (set! server-list (cons elem server-list)))))
     args)

    ;; Iterate over server list and ..
    (for-each
     (lambda (server)
       ;; ... for each server, iterate over port list and ...
       (for-each
	(lambda (port)
	  ;; ... bind each port to each server.
	  (bind-server! port server))
	port-list))
     server-list)))

;;
;; === Create a simple tcp port
;;
(define (create-tcp-port! basename port)
  (define portname '())
  (set! portname (string-append basename (number->string port)))
  (if (not (serveez-port? portname))
      (define-port! portname 
	`((proto . tcp) 
	  (port . ,port))))
  portname)

;;
;; === Bind some servers to a range of tcp network ports
;;
(define (bind-tcp-port-range! from to . args)
  (do ((no from (+ no 1)))
      ((> no to))
    (for-each
     (lambda (server)
       (bind-server! (create-tcp-port! "guile-tcp-port-" no) server))
     args)))
