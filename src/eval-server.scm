;; -*-scheme-*-
;;
;; eval-server.scm - Example server for evaluating Scheme expressions
;;
;; Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>,
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
;; $Id: eval-server.scm,v 1.1 2001/07/24 13:31:07 ela Exp $
;;

;; Some awkward compatibility kluges for making this run with Guile
;; 1.4 and 1.6/later.
;;
(if (defined? 'micro-version)
    (use-modules (ice-9 safe))
    (begin
      (let ((real-eval eval))
        (set! eval (lambda (expr env)
                     (real-eval expr))))
      (define (object->string obj)
        (format #f "~s" obj))
      (define (make-safe-module) #t)))

(primitive-load "serveez.scm")

(define *greeting* "Hello, type `quit' to end the connection.
Type Scheme expression to see them evaluated (but only one-liners.)")
(define *eval-prompt* "eval: ")
(define *quit-command* "quit")

(define (eval-global-init servertype)
  (println "Running eval global init " servertype ".")
  0)

(define (eval-init server)
  (println "Running eval init " server ".")
  0)

(define (eval-global-finalize servertype)
  (println "Running eval global finalizer " servertype ".")
  0)

(define (eval-finalize server)
  (println "Running eval finalizer " server ".")
  0)

(define (eval-detect-proto server sock)
  (println "Detecting eval protocol ...")
  1)

(define (eval-info-server server)
  (println "Running eval server info " server ".")
  " This is the eval server.")

(define (eval-handle-request sock request len)
  (define ret '())
  (if (and (>= (binary-length request) 4) (= 0 (binary-search request "quit")))
    -1
    (let ((safe-module (make-safe-module)))
      (catch #t
             (lambda ()
               (let ((expr (read (open-input-string
                                  (binary->string request)))))
                 (let ((res (eval expr safe-module)))
                   (svz:sock:print sock
                                   (string->binary
                                    (string-append "=> "
                                                   (object->string res)
                                                   "\n"
                                                   *eval-prompt*))))))
             (lambda args
               (svz:sock:print sock
                               (string->binary
                                (string-append "Exception "
                                               (object->string args)
                                               "\n"
                                               *eval-prompt*)))))
      0)))

(define (eval-connect-socket server sock)
  (println "Running connect socket.")
  (svz:sock:boundary sock "\n")
  (svz:sock:handle-request sock eval-handle-request)
  (svz:sock:print sock (string-append *greeting* "\n" *eval-prompt*))
  0)

;; Port configuration.
(define-port! 'eval-port '((proto . tcp)
                           (port  . 2001)))

;; Servertype definitions.
(define-servertype! '(
  (prefix      . "eval")
  (description . "guile eval server")
  (detect-proto    . eval-detect-proto)
  (global-init     . eval-global-init)
  (init            . eval-init)
  (finalize        . eval-finalize)
  (global-finalize . eval-global-finalize)
  (connect-socket  . eval-connect-socket)
  (info-server     . eval-info-server)
  (configuration   . (
    ;; (key . (type defaultable default))
    (eval-integer       . (integer #t 0))
    (eval-integer-array . (intarray #t (1 2 3 4 5)))
    (eval-string        . (string #t "default-eval-string"))
    (eval-string-array  . (strarray #t ("guile" "eval" "server")))
    (eval-hash          . (hash #t (("eval" . "fast") ("guile" . "tricky"))))
    (eval-port          . (portcfg #t eval-port))
    (eval-boolean       . (boolean #t #t))
  ))))

;; Server instantiation.
(define-server! 'eval-server)

;; Bind server to port.
(bind-server! 'eval-port 'eval-server)

