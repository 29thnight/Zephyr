import { defineConfig } from 'vitepress'

export default defineConfig({
  title: "Zephyr",
  description: "A fast, lightweight script language for game engine embedding",
  appearance: false,
  
  themeConfig: {
    siteTitle: false,
    search: {
      provider: 'local'
    },
    // Social links removed as requested (Github icon removal)
    socialLinks: []
  },

  locales: {
    'en-us': {
      label: 'English',
      lang: 'en-US',
      link: '/en-us/',
      themeConfig: {
        nav: [
          { text: 'Documentation', link: '/en-us/introduction/overview' },
          { text: 'Github', link: 'https://github.com/29thnight/Zephyr' }
        ],
        sidebar: [
          {
            text: 'INTRODUCTION',
            items: [
              { text: 'Overview', link: '/en-us/introduction/overview' },
              { text: 'Getting Started', link: '/en-us/introduction/quickstart' }
            ]
          },
          {
            text: 'LANGUAGE GUIDE',
            items: [
              { text: 'Syntax', link: '/en-us/language/syntax' },
              { text: 'Types', link: '/en-us/language/types' },
              { text: 'Declarations', link: '/en-us/language/declarations' },
              { text: 'Struct & Enum', link: '/en-us/language/struct-enum' },
              { text: 'Pattern Matching', link: '/en-us/language/pattern-matching' },
              { text: 'Control Flow', link: '/en-us/language/control-flow' },
              { text: 'Functions', link: '/en-us/language/functions' },
              { text: 'Traits & Impl', link: '/en-us/language/traits' },
              { text: 'Coroutines', link: '/en-us/language/coroutines' },
              { text: 'Modules', link: '/en-us/language/modules' }
            ]
          },
          {
            text: 'ADVANCED',
            items: [
              { text: 'Embedding ZephyrVM', link: '/en-us/advanced/embedding' },
              { text: 'C++ Class Bindings', link: '/en-us/advanced/bindings' },
              { text: 'Host Handle Policy', link: '/en-us/advanced/host-handles' },
              { text: 'Garbage Collection', link: '/en-us/advanced/garbage-collection' }
            ]
          }
        ]
      }
    },
    'ko-kr': {
      label: '한국어',
      lang: 'ko-KR',
      link: '/ko-kr/',
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
          }
        ]
      }
    }
  }
})
