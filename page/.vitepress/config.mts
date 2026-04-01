import { defineConfig } from 'vitepress'

export default defineConfig({
  title: "Zephyr",
  description: "A fast, lightweight script language for game engine embedding",
  base: '/Zephyr/',
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
              { text: 'Operators', link: '/en-us/language/operators' },
              { text: 'Types', link: '/en-us/language/types' },
              { text: 'String', link: '/en-us/language/string' },
              { text: 'Array', link: '/en-us/language/array' },
              { text: 'Declarations', link: '/en-us/language/declarations' },
              { text: 'Struct & Enum', link: '/en-us/language/struct-enum' },
              { text: 'Pattern Matching', link: '/en-us/language/pattern-matching' },
              { text: 'Control Flow', link: '/en-us/language/control-flow' },
              { text: 'Functions & Closures', link: '/en-us/language/functions' },
              { text: 'Traits & Impl', link: '/en-us/language/traits' },
              { text: 'Generics', link: '/en-us/language/generics' },
              { text: 'Coroutines', link: '/en-us/language/coroutines' },
              { text: 'Modules', link: '/en-us/language/modules' }
            ]
          },
          {
            text: 'STANDARD LIBRARY',
            items: [
              { text: 'Standard Library', link: '/en-us/stdlib/index' }
            ]
          },
          {
            text: 'ADVANCED',
            items: [
              { text: 'Embedding ZephyrVM', link: '/en-us/advanced/embedding' },
              { text: 'C++ Class Bindings', link: '/en-us/advanced/bindings' },
              { text: 'Host Handle Policy', link: '/en-us/advanced/host-handles' },
              { text: 'Garbage Collection', link: '/en-us/advanced/garbage-collection' },
              { text: 'VM Optimization', link: '/en-us/advanced/vm-optimization' }
            ]
          },
          {
            text: 'INTERNALS',
            items: [
              { text: 'Architecture', link: '/en-us/internals/architecture' },
              { text: 'Current State', link: '/en-us/internals/current' },
              { text: 'Future Direction', link: '/en-us/internals/future' },
              { text: 'Implementation Plan', link: '/en-us/internals/plan' },
              { text: 'Process Log', link: '/en-us/internals/process' },
              { text: 'Mini Spec', link: '/en-us/internals/mini_spec' },
              { text: 'Package Layout', link: '/en-us/internals/package_layout' }
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
              { text: '연산자', link: '/ko-kr/language/operators' },
              { text: '타입 모델', link: '/ko-kr/language/types' },
              { text: '문자열', link: '/ko-kr/language/string' },
              { text: '배열', link: '/ko-kr/language/array' },
              { text: '선언문', link: '/ko-kr/language/declarations' },
              { text: '구조체 & 열거형', link: '/ko-kr/language/struct-enum' },
              { text: '패턴 매칭', link: '/ko-kr/language/pattern-matching' },
              { text: '제어 흐름', link: '/ko-kr/language/control-flow' },
              { text: '함수 및 클로저', link: '/ko-kr/language/functions' },
              { text: '트레이트', link: '/ko-kr/language/traits' },
              { text: '제네릭', link: '/ko-kr/language/generics' },
              { text: '코루틴', link: '/ko-kr/language/coroutines' },
              { text: '모듈 로드', link: '/ko-kr/language/modules' }
            ]
          },
          {
            text: '표준 라이브러리 (STANDARD LIBRARY)',
            items: [
              { text: '표준 라이브러리', link: '/ko-kr/stdlib/index' }
            ]
          },
          {
            text: '고급 (ADVANCED)',
            items: [
              { text: 'ZephyrVM 내장', link: '/ko-kr/advanced/embedding' },
              { text: 'C++ 클래스 바인딩', link: '/ko-kr/advanced/bindings' },
              { text: '호스트 핸들 정책', link: '/ko-kr/advanced/host-handles' },
              { text: '가비지 컬렉터', link: '/ko-kr/advanced/garbage-collection' },
              { text: 'VM 최적화', link: '/ko-kr/advanced/vm-optimization' }
            ]
          },
          {
            text: '내부 스펙 (INTERNALS)',
            items: [
              { text: '언어 아키텍처', link: '/ko-kr/internals/architecture' },
              { text: '현재 구현 상태', link: '/ko-kr/internals/current' },
              { text: '향후 발전 방향', link: '/ko-kr/internals/future' },
              { text: '구현 달성 계획표', link: '/ko-kr/internals/plan' },
              { text: '구현 프로세스 로그', link: '/ko-kr/internals/process' },
              { text: '미니 스펙 요약', link: '/ko-kr/internals/mini_spec' },
              { text: '패키지 구조', link: '/ko-kr/internals/package_layout' }
            ]
          }
        ]
      }
    }
  }
})
