import { defineConfig } from 'vitepress'

export default defineConfig({
  base: '/Zephyr/',
  title: "Zephyr",
  description: "A fast, lightweight script language for game engine embedding",
  appearance: false,
  
  themeConfig: {
    siteTitle: false,
    search: {
      provider: 'local'
    },
    socialLinks: []
  },

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      themeConfig: {
        nav: [
          { text: 'Documentation', link: '/introduction/overview' },
          { text: 'Github', link: 'https://github.com/29thnight/Zephyr' }
        ],
        sidebar: [
          {
            text: 'INTRODUCTION',
            items: [
              { text: 'Overview', link: '/introduction/overview' },
              { text: 'Getting Started', link: '/introduction/quickstart' }
            ]
          },
          {
            text: 'LANGUAGE GUIDE',
            items: [
              { text: 'Syntax', link: '/language/syntax' },
              { text: 'Types', link: '/language/types' },
              { text: 'Declarations', link: '/language/declarations' },
              { text: 'Struct & Enum', link: '/language/struct-enum' },
              { text: 'Pattern Matching', link: '/language/pattern-matching' },
              { text: 'Control Flow', link: '/language/control-flow' },
              { text: 'Functions', link: '/language/functions' },
              { text: 'Traits & Impl', link: '/language/traits' },
              { text: 'Coroutines', link: '/language/coroutines' },
              { text: 'Modules', link: '/language/modules' }
            ]
          },
          {
            text: 'ADVANCED',
            items: [
              { text: 'Embedding ZephyrVM', link: '/advanced/embedding' },
              { text: 'C++ Class Bindings', link: '/advanced/bindings' },
              { text: 'Host Handle Policy', link: '/advanced/host-handles' },
              { text: 'Garbage Collection', link: '/advanced/garbage-collection' }
            ]
          },
          {
            text: 'STANDARD LIBRARY',
            items: [
              { text: 'Math & String', link: '/standard-library/math-string' },
              { text: 'Collections', link: '/standard-library/collections' },
              { text: 'JSON & I/O', link: '/standard-library/json-io' },
              { text: 'GC & Profiler', link: '/standard-library/gc-profiler' }
            ]
          }
        ]
      }
    },
    'ko-kr': {
      label: '한국어',
      lang: 'ko-KR',
      themeConfig: {
        nav: [
          { text: '문서', link: '/ko-kr/introduction/overview' },
          { text: 'Github', link: 'https://github.com/29thnight/Zephyr' }
        ],
        sidebar: [
          {
            text: '소개 (INTRODUCTION)',
            items: [
              { text: '개요 (Overview)', link: '/ko-kr/introduction/overview' },
              { text: '시작하기 (Quickstart)', link: '/ko-kr/introduction/quickstart' }
            ]
          },
          {
            text: '언어 가이드 (LANGUAGE GUIDE)',
            items: [
              { text: '문법 및 렉시컬', link: '/ko-kr/language/syntax' },
              { text: '타입 모델', link: '/ko-kr/language/types' },
              { text: '선언문', link: '/ko-kr/language/declarations' },
              { text: '구조체 & 열거형', link: '/ko-kr/language/struct-enum' },
              { text: '패턴 매칭', link: '/ko-kr/language/pattern-matching' },
              { text: '제어 흐름', link: '/ko-kr/language/control-flow' },
              { text: '함수 및 클로저', link: '/ko-kr/language/functions' },
              { text: '트레이트', link: '/ko-kr/language/traits' },
              { text: '코루틴', link: '/ko-kr/language/coroutines' },
              { text: '모듈 로드', link: '/ko-kr/language/modules' }
            ]
          },
          {
            text: '고급 (ADVANCED)',
            items: [
              { text: 'ZephyrVM 내장', link: '/ko-kr/advanced/embedding' },
              { text: 'C++ 클래스 바인딩', link: '/ko-kr/advanced/bindings' },
              { text: '호스트 핸들 정책', link: '/ko-kr/advanced/host-handles' },
              { text: '가비지 컬렉터', link: '/ko-kr/advanced/garbage-collection' }
            ]
          },
          {
            text: '표준 라이브러리 (STANDARD LIBRARY)',
            items: [
              { text: '수학 및 문자열 (Math & String)', link: '/ko-kr/standard-library/math-string' },
              { text: '컬렉션 (Collections)', link: '/ko-kr/standard-library/collections' },
              { text: 'JSON 및 I/O (JSON & I/O)', link: '/ko-kr/standard-library/json-io' },
              { text: '트러블슈팅 (GC & Profiler)', link: '/ko-kr/standard-library/gc-profiler' }
            ]
          }
        ]
      }
    }
  }
})
