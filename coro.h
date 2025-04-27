#ifndef GRAMAS_CORO_H
#define GRAMAS_CORO_H

/* This header defines the necessary macros that make coroutines work. The
 * trick that makes them not only work but also work fast is so dead-simple I
 * cannot imagine why this sort of construct is not used more often. It boils
 * down to the following:
 *
 *     1. Store address of the next instruction after a return statement.
 *     2. Go straight to the stored address when entering the function.
 *
 * Of course, you must initialize the address to zero so you could tell if
 * you're entering the coroutine for the first time but the above two lines
 * describe the process afterwards in its entirety. Now the questions that need
 * answering are: how do you use them and why would you use them in the first
 * place?
 *
 * How to use them?
 * ----------------
 *
 * Introduce an integer state variable of the type coro_state_t, wrap the
 * function body in CO_BEGIN(state) and CO_END, and use CO_YIELD(state,
 * ret_val) where you'd normally use return in a synchronous function like
 * this:
 *
 *      struct fib_generator {
 *          coro_state_t state;
 *          int prev;
 *          int curr;
 *          int next;
 *      };
 *
 *      int fib_next(struct fib_generator *g)
 *      {
 *          // Introduces the coroutine body
 *          CO_BEGIN(g->state)
 *
 *          g->prev = 0;
 *          g->curr = 1;
 *
 *          while (1) {
 *              // Yield a value. The next time this function is called
 *              // execution will continue from the next line.
 *              CO_YIELD(g->state, g->curr);
 *              g->next = g->prev + g->curr;
 *              g->prev = g->curr;
 *              g->curr = g->next;
 *
 *              // Error condition. Always return -1 from here onwards.
 *              if (g->prev > g->curr)
 *                  CO_RETURN(g->state, -1);
 *          }
 *
 *          CO_END  // NEVER EVER reach this statement! If you do, the program
 *                  // will be aborted!
 *      }
 *
 * When using this header observe the following rules:
 *
 *      1.  Always initialize the state variable to 0 before you first call the
 *          coroutine. If you do not, undefined behaviour will ensue.
 *
 *      2.  Do not use the switch statement inside coroutine body.
 *
 *      3.  Put all local function state into a struct if reentrancy is
 *          something you desire.
 *
 *      4.  Never *EVER* reach CO_END.
 *
 *          It's there to abort the program if the state gets corrupted or if
 *          the coroutine is called after it's done. If you want well-defined
 *          behaviour that does not kill the program use CO_RETURN, which will
 *          cause the coroutine to always return the same value afterwards, or
 *          use a return statement after a CO_YIELD.
 *
 * Implementation notes
 * --------------------
 *
 * When compiling with GCC computed goto is used instead of a switch statement
 * in order to turn conditional branches into a jump. If you mess up the state
 * variable, the consequences will be even more catastrophic than if the switch
 * and case implementation was used. The computed-goto version does not prevent
 * the use of switch statements inside coroutine body but you'd be wise to
 * refrain from using it for the sake of portability.
 *
 * Why use coroutines and how do they work?
 * ----------------------------------------
 *
 * Coroutines are useful whenever you want to build a state machine that either
 * operates asynchronously and/or would be rendered unreadable if written in
 * the "traditional" style. If you have ever seen a function littered with if
 * statements or written as a collection of disjointed code blocks that are
 * reached via an explicit switch statement on some state variable, then you
 * know what kind of function I am talking about here. The asynchronous and
 * explicit nature of the state machine causes one to apply transformations
 * that render an otherwise simple algorithm nigh unreadable and prevents use
 * of many high-level control flow structures like for loops and if statements.
 * Coroutines are here to solve that by letting you write code in the
 * "traditional" style with the caveat that the function can return a value and
 * then continue on from where it last returned when called again.
 *
 * Imagine you're writing a function that turns text into tokens but you don't
 * want to: a) read the whole text into memory because the text can be of
 * arbitrary length and storing it RAM will kill your machine; b) the function
 * must return tokens one by one (for the same reason you'd rather not read the
 * whole thing into memory). If you COULD just read the whole thing and return
 * a long list, then you'd write something like this:
 *
 *      enum token_kind {
 *          UNKNOWN,
 *          IDENTIFIER,
 *          NUMBER
 *      };
 *
 *      void tokenize(const char *text, size_t length, struct token_list *tokens)
 *      {
 *          size_t i;
 *          struct token token;
 *
 *          token_init(&token);
 *
 *          for (i = 0; i < length;) {
 *              for (; i < length; i++)
 *                  if (!isspace(text[i]))
 *                      break;
 *
 *              if (i >= length)
 *                  return;
 *
 *              token_clear(&token);
 *
 *              if (isalpha(text[i])) {
 *                  token.kind = IDENTIFIER;
 *
 *                  for (; i < length; i++) {
 *                      if (isalpha(text[i]))
 *                          token_add_ch(&token, text[i]);
 *                      else
 *                          break;
 *                  }
 *
 *                  token_list_add(tokens, &token);
 *              } else if (isdigit(text[i])) {
 *                  token.kind = NUMBER;
 *
 *                  for (; i < length; i++) {
 *                      if (isdigit(text[i]))
 *                          token_add_ch(&token, text[i]);
 *                      else
 *                          break;
 *                  }
 *
 *                  token_list_add(tokens, &token);
 *              } else {
 *                  token.kind = UNKNOWN;
 *                  token_add_ch(&token, text[i++]);
 *              }
 *          }
 *
 *          token_destroy(&token);
 *
 *          return;
 *      }
 *
 * Pure and simple. Little can go wrong here. And this is what it would look
 * like if transformed into an explicit state machine so it would accept input
 * one character at a time:
 *
 *      enum tokenizer_state_t {
 *          SKIP_SPACES,
 *          READ_IDENTIFIER,
 *          READ_NUMBER,
 *          DETERMINE_KIND
 *      };
 *
 *      struct tokenizer {
 *          struct token_t token;
 *          struct token_list tokens;
 *          enum tokenizer_state_t state;
 *      };
 *
 *      int tokenize(struct tokenizer *t, int c)
 *      {
 *          switch (t->state) {
 *          case SKIP_SPACES:
 *              if (isspace(c))
 *                  break;
 *
 *              goto DETERMINE_KIND;
 *
 *          case READ_IDENTIFIER:
 *              if (isalpha(c)) {
 *                  token_add_ch(&t->token, c);
 *              } else {
 *                  token_list_add(&t->tokens, &t->token);
 *                  goto DETERMINE_KIND;
 *              }
 *
 *              break;
 *
 *          case READ_NUMBER:
 *              if (isdigit(c)) {
 *                  token_add_ch(&t->token, c);
 *              } else {
 *                  token_list_add(&t->tokens, &t->token);
 *                  goto DETERMINE_KIND;
 *              }
 *
 *              break;
 *
 *          case DETERMINE_KIND:
 *              token_clear(&t->token);
 *              token_add_ch(&t->token, c)
 *
 *              if (isalpha(c)) {
 *                  t->kind = IDENTIFIER;
 *                  t->state = READ_IDENTIFIER;
 *                  break;
 *              }
 *
 *              if (isdigit(c)) {
 *                  t->kind = IDENTIFIER;
 *                  t->state = READ_IDENTIFIER;
 *                  break;
 *              }
 *
 *              t->kind = UNKNOWN;
 *              token_list_add(&t->tokens, &t->token);
 *              token_clear(&t->token);
 *              t->state = SKIP_SPACES;
 *
 *              break;
 *          }
 *
 *          return 0;
 *      }
 *
 * Ugly as sin and barely comprehensible. Forget about loops and if statements.
 * It's all switch, case, and goto. A simple algorithm became a monstrosity.
 * Now imagine a world where C has a "yield" keyword like so many languages
 * these days do:
 *
 *      int tokenize(struct tokenizer *t, int c, struct token *token)
 *      {
 *          token_init(&token);
 *
 *          while (c != EOF) {
 *              if (!isspace(c))
 *                  break;
 *
 *              token_clear(&token);
 *
 *              if (isalpha(c)) {
 *                  token.kind = IDENTIFIER;
 *
 *                  while (isalpha(c)) {
 *                      token_add_ch(&token, c);
 *                      yield 0;
 *                  }
 *              } else if (isdigit(c)) {
 *                  token.kind = NUMBER;
 *
 *                  while (isdigit(c)) {
 *                      token_add_ch(&token, c);
 *                      yield 0;
 *                  }
 *              } else {
 *                  token.kind = UNKNOWN;
 *                  token_add_ch(&token, c);
 *              }
 *
 *              yield 1;
 *          }
 *
 *          token_destroy(&token);
 *
 *          yield -1;
 *      }
 *
 * What a blessed world it would be! Alas, us, C programmers, are not spoiled
 * by such comforts and must make do with the good ol' switch n' case
 * construction. Do we, though? Surely there must be a better way. I have seen
 * people reach for the following construction:
 *
 *      int tokenize(struct tokenizer *t, int c, struct token *token)
 *      {
 *          switch (t->state) {
 *          case INIT: goto init;
 *          case READ_IDENTIFIER: goto read_identifier;
 *          case READ_NUMBER: goto read_number;
 *          case SKIP_SPACES: goto skip_spaces;
 *          }
 *
 *      init:
 *          token_init(&token);
 *
 *          while (c != EOF) {
 *              if (!isspace(c))
 *                  break;
 *
 *              token_clear(&token);
 *
 *              if (isalpha(c)) {
 *                  token.kind = IDENTIFIER;
 *
 *                  while (isalpha(c)) {
 *                      token_add_ch(&token, c);
 *                      t->state = READ_IDENTIFIER;
 *                      return 0;
 *      read_identifier:;
 *                  }
 *              } else if (isdigit(c)) {
 *                  token.kind = NUMBER;
 *
 *                  while (isdigit(c)) {
 *                      token_add_ch(&token, c);
 *                      return 0;
 *      read_number:;
 *                  }
 *              } else {
 *                  t->state = SKIP_SPACES;
 *                  token.kind = UNKNOWN;
 *                  token_add_ch(&token, t->text[i++]);
 *              }
 *
 *              return 1;
 *      skip_spaces:;
 *          }
 *
 *          token_destroy(&token);
 *
 *          return 0;
 *      }
 *
 * Hell of a lot better than the explicit switch and case construction but this
 * approach has some damning drawbacks: you must still use a switch statement,
 * you must also remember to set the state when needed and, if adding a new
 * statement, remember to include it in the initial switch statement. Error
 * prone and tedious but it works. And when I say "Error prone", I mean it. The
 * code, as written above, contains a nasty bug and I bet most people didn't
 * even notice it the first time they read it. Still, despite the drawbacks,
 * this method does show promise --- the structure of the algorithm is left
 * mostly intact by it and can be recognized just by looking at its loops and
 * if statements.
 *
 * In order to overcome the burdens of manually maintaining a list of states,
 * a switch statement, and setting the state when appropriate, a method must be
 * found to automate as much of this process as possible. First off, there's an
 * uncommonly used feature of the switch statement in the C language: its cases
 * can be put almost anywhere inside the switch block, even inside other
 * blocks. This means we can get rid of the labels and replace them with cases
 * instead:
 *
 *      int tokenize(struct tokenizer *t, int c, struct token *token)
 *      {
 *          switch (t->state) {
 *          case INIT:
 *              token_init(&token);
 *
 *              while (c != EOF) {
 *                  if (!isspace(c))
 *                      break;
 *
 *                  token_clear(&token);
 *
 *                  if (isalpha(c)) {
 *                      t->state = READ_IDENTIFIER;
 *                      token.kind = IDENTIFIER;
 *
 *                      while (isalpha(c)) {
 *                          token_add_ch(&token, c);
 *                          return 0;
 *          case READ_IDENTIFIER:;
 *                      }
 *                  } else if (isdigit(c)) {
 *                      token.kind = NUMBER;
 *
 *                      while (isdigit(c)) {
 *                          token_add_ch(&token, c);
 *                          return 0;
 *          case READ_NUMBER:;
 *                      }
 *                  } else {
 *                      t->state = SKIP_SPACES;
 *                      token.kind = UNKNOWN;
 *                      token_add_ch(&token, t->text[i++]);
 *                  }
 *
 *                  return 1;
 *          case SKIP_SPACES:;
 *              }
 *
 *              token_destroy(&token);
 *          }
 *
 *          return 0;
 *      }
 *
 * Much better but the manual management of the state variable and the list of
 * possible states remains an issue. There is a way to automatically generate
 * them using macros. The "__LINE__" macro is replaced by a different number in
 * every line it is used. When combined with the fact that the state variable
 * and case labels are all integers, the following can be obtained:
 *
 *      #define YIELD(__state, __ret)   \
 *          do {    \
 *             (__state) = __LINE__; \
 *             return __ret;   \
 *             case __LINE__:;  \
 *          } while (0)
 *
 *      int tokenize(struct tokenizer *t, int c, struct token *token)
 *      {
 *          switch (t->state) {
 *          case 0:
 *              token_init(&token);
 *
 *              while (c != EOF) {
 *                  if (!isspace(c))
 *                      break;
 *
 *                  token_clear(&token);
 *
 *                  if (isalpha(c)) {
 *                      token.kind = IDENTIFIER;
 *
 *                      while (isalpha(c)) {
 *                          token_add_ch(&token, c);
 *                          YIELD(t->state, 0);
 *                      }
 *                  } else if (isdigit(c)) {
 *                      token.kind = NUMBER;
 *
 *                      while (isdigit(c)) {
 *                          token_add_ch(&token, c);
 *                          YIELD(t->state, 0);
 *                      }
 *                  } else {
 *                      token.kind = UNKNOWN;
 *                      token_add_ch(&token, t->text[i++]);
 *                  }
 *
 *                  YIELD(t->state, 1);
 *              }
 *
 *              token_destroy(&token);
 *
 *          default:
 *              abort();
 *          }
 *
 *          return 0;
 *      }
 *
 * Far fewer ways to shoot oneself in the foot remain. If you replace the
 * explicit switch statement with a pair of macros and introduce a new macro to
 * enable easy stopping of the state machine, you get the following:
 *
 *      #define CO_BEGIN(__state) switch (__state) { case 0:;
 *
 *      #define CO_YIELD(__state, __ret)   \
 *          do {    \
 *             (__state) = __LINE__; \
 *             return __ret;   \
 *             case __LINE__:;  \
 *          } while (0)
 *
 *      #define CO_RETURN(__state, __ret)   \
 *          do {    \
 *             (__state) = __LINE__; \
 *             case __LINE__:   \
 *                 return __ret;   \
 *          } while (0)
 *
 *      #define CO_END default: abort(); }
 *
 *      int tokenize(struct tokenizer *t, int c, struct token *token)
 *      {
 *          CO_BEGIN(t->state)
 *
 *          token_init(&token);
 *
 *          while (c != EOF) {
 *              if (!isspace(c))
 *                  break;
 *
 *              token_clear(&token);
 *
 *              if (isalpha(c)) {
 *                  token.kind = IDENTIFIER;
 *
 *                  while (isalpha(c)) {
 *                      token_add_ch(&token, c);
 *                      CO_YIELD(t->state, 0);
 *                  }
 *              } else if (isdigit(c)) {
 *                  token.kind = NUMBER;
 *
 *                  while (isdigit(c)) {
 *                      token_add_ch(&token, c);
 *                      CO_YIELD(t->state, 0);
 *                  }
 *              } else {
 *                  token.kind = UNKNOWN;
 *                  token_add_ch(&token, t->text[i++]);
 *              }
 *
 *              CO_YIELD(t->state, 1);
 *          }
 *
 *          token_destroy(&token);
 *
 *          CO_RETURN(t->state, 0);
 *
 *          CO_END
 *      }
 *
 * Very little manual work is left for the programmer. All you have to do now
 * to transform a synchronous function that iterates over a list into an
 * asynchronous function that consumes individual items is the following:
 *
 *      1.  Introduce an integer state variable.
 *      2.  Wrap the function in CO_BEGIN() and CO_END;
 *      3.  Replace all "return"'s with CO_YIELD().
 *
 * Some might say that unmatched braces in macros and macros that alter control
 * flow are deadly sins. No coding standard would allow such code and it would
 * never pass code review. Most of the time I would be inclined to agree but in
 * this case the gains in clarity are so obvious that it would be a mistake to
 * not even consider using such a construction. If someone were to shoot down a
 * proposal to use this scheme, then I'd call that person either a cargo-cult
 * programmer or a bureaucrat because he had clearly demonstrated that he
 * forgot that coding standards serve a purpose: to make the lives of
 * programmers easier and more productive in the long run by increasing
 * clarity. Coroutines do exactly that, however implemented.
 * */

#include <stdint.h>

typedef uintptr_t coro_state_t;

/* A significantly better performing version that utilizes computed goto. */
#if __GNUC__ && !USE_SWITCH_BASED_CORO

#define CO_BEGIN(__state) if (__state != 0) goto *((void *)(__state));

#define __CO_LABEL(__line) __coro_continue_ ## __line
#define CO_LABEL(__line) __CO_LABEL(__line)

#define CO_YIELD(__state, __ret)	\
	do {	\
		__state = (coro_state_t)&&CO_LABEL(__LINE__);	\
		return __ret;	\
		CO_LABEL(__LINE__):;	\
	} while (0)

#define CO_RETURN(__state, __ret)	\
	do {	\
		__state = (coro_state_t)&&CO_LABEL(__LINE__);	\
		CO_LABEL(__LINE__):;\
		return __ret;	\
	} while (0)

#define CO_END abort();

#else /* Default variety using a switch statement. Will work everywhere but is
		 not nearly as performant and prevents the use of switch statements
		 inside coroutines. */

#define CO_BEGIN(__state) switch (__state) { case 0:;

#define CO_YIELD(__state, __ret)	\
	do {	\
		__state = __LINE__;	\
		return __ret;	\
		case __LINE__:;	\
	} while (0)

#define CO_RETURN(__state, __ret)	\
	do {	\
		__state = __LINE__;	\
		case __LINE__:	\
			return __ret;	\
	} while (0)

#define CO_END default: abort(); }

#endif /* Default variety */

#endif // GRAMAS_CORO_H
